#include "SpoutSenderComponent.h"

#include "Spout2_DX12.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Actor.h"
#include "Engine/TextureRenderTarget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RHICommandList.h"
#include "Rendering/SlateRenderer.h"
#include "TextureResource.h"
#include "Logging/LogMacros.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Editor.h"
#include "UnrealClient.h"
#endif

#if PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Windows/WindowsHWrapper.h"

THIRD_PARTY_INCLUDES_START
#include "Windows/AllowWindowsPlatformTypes.h"

#include <d3d11.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi.h>

#include "SpoutDX.h"
#include "SpoutDX12.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#ifdef IsMinimized
#undef IsMinimized
#endif

#include "Windows/HideWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_END
#endif

namespace
{
    static const TCHAR* GetSenderWorldTypeName(const UWorld* World)
    {
        if (!World)
        {
            return TEXT("None");
        }

        switch (World->WorldType)
        {
        case EWorldType::None:
            return TEXT("None");
        case EWorldType::Game:
            return TEXT("Game");
        case EWorldType::Editor:
            return TEXT("Editor");
        case EWorldType::PIE:
            return TEXT("PIE");
        case EWorldType::EditorPreview:
            return TEXT("EditorPreview");
        case EWorldType::GamePreview:
            return TEXT("GamePreview");
        case EWorldType::GameRPC:
            return TEXT("GameRPC");
        case EWorldType::Inactive:
            return TEXT("Inactive");
        default:
            return TEXT("Other");
        }
    }

    static const TCHAR* GetSenderSourceName(ESpoutSenderSourceType SourceType)
    {
        switch (SourceType)
        {
        case ESpoutSenderSourceType::RenderTarget:
            return TEXT("RenderTarget");
        case ESpoutSenderSourceType::GameViewport:
            return TEXT("GameViewport");
        case ESpoutSenderSourceType::EditorViewport:
            return TEXT("EditorViewport");
        default:
            return TEXT("Unknown");
        }
    }

    static bool IsSenderEditorWorld(const UWorld* World)
    {
        return World && World->WorldType == EWorldType::Editor;
    }

    static bool IsSenderPreviewWorld(const UWorld* World)
    {
        return World && World->WorldType == EWorldType::EditorPreview;
    }

    static FString BuildSenderDebugContext(const USpoutSenderComponent* Component)
    {
        const UWorld* World = Component ? Component->GetWorld() : nullptr;
        return FString::Printf(
            TEXT("Sender='%s' Source=%s Owner='%s' World='%s' WorldType=%s"),
            Component ? *Component->CurrentSenderName : TEXT("<null>"),
            Component ? GetSenderSourceName(Component->SourceType) : TEXT("Unknown"),
            *GetNameSafe(Component ? Component->GetOwner() : nullptr),
            World ? *World->GetName() : TEXT("<none>"),
            GetSenderWorldTypeName(World));
    }
}

USpoutSenderComponent::USpoutSenderComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickInterval = 0.0f;
    bTickInEditor = true;
}

bool USpoutSenderComponent::IsEditorWorld() const
{
    return IsSenderEditorWorld(GetWorld());
}

bool USpoutSenderComponent::IsPreviewWorld() const
{
    return IsSenderPreviewWorld(GetWorld());
}

bool USpoutSenderComponent::IsSupportedWorld() const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    if (World->IsGameWorld())
    {
        return true;
    }

    switch (StartupPolicy)
    {
    case ESpoutWorldBootstrapPolicy::GameOnly:
        return false;
    case ESpoutWorldBootstrapPolicy::EditorAndGame:
        return IsSenderEditorWorld(World);
    case ESpoutWorldBootstrapPolicy::EditorGameAndSinglePreview:
        return IsSenderEditorWorld(World) || IsSenderPreviewWorld(World);
    default:
        return false;
    }
}

bool USpoutSenderComponent::IsD3D12Active() const
{
    return GDynamicRHI && FString(GDynamicRHI->GetName()) == TEXT("D3D12");
}

bool USpoutSenderComponent::IsUsingEditorViewportSource() const
{
#if WITH_EDITOR
    return SourceType == ESpoutSenderSourceType::EditorViewport && (IsEditorWorld() || IsPreviewWorld());
#else
    return false;
#endif
}

bool USpoutSenderComponent::IsUsingGameViewportSource() const
{
    return SourceType == ESpoutSenderSourceType::GameViewport;
}

bool USpoutSenderComponent::ShouldUseSlateBackBufferGameViewportPath() const
{
#if PLATFORM_WINDOWS
#if WITH_EDITOR
    if (GIsEditor)
    {
        return false;
    }
#endif
    return IsUsingGameViewportSource() && !IsEditorWorld() && !IsPreviewWorld();
#else
    return false;
#endif
}

bool USpoutSenderComponent::HasValidConfiguredSource() const
{
    switch (SourceType)
    {
    case ESpoutSenderSourceType::RenderTarget:
        return CurrentRenderTarget != nullptr;
    case ESpoutSenderSourceType::GameViewport:
        return true;
    case ESpoutSenderSourceType::EditorViewport:
        return IsUsingEditorViewportSource();
    default:
        return false;
    }
}

bool USpoutSenderComponent::ResolveCurrentSource(
    FTextureRHIRef& OutTexture,
    int32& OutWidth,
    int32& OutHeight,
    EPixelFormat& OutFormat) const
{
    OutTexture.SafeRelease();
    OutWidth = 0;
    OutHeight = 0;
    OutFormat = PF_Unknown;

#if WITH_EDITOR
    if (IsUsingEditorViewportSource())
    {
        if (!GEditor)
        {
            return false;
        }

        FViewport* ActiveViewport = GEditor->GetActiveViewport();
        if (!ActiveViewport)
        {
            return false;
        }

        FTextureRHIRef ViewportTexture = ActiveViewport->GetRenderTargetTexture();
        if (!ViewportTexture.IsValid())
        {
            return false;
        }

        const FIntPoint Size = ActiveViewport->GetSizeXY();
        if (Size.X <= 0 || Size.Y <= 0)
        {
            return false;
        }

        OutTexture = ViewportTexture;
        OutWidth = Size.X;
        OutHeight = Size.Y;
        OutFormat = ViewportTexture->GetFormat();
        return true;
    }
#endif

    if (ShouldUseSlateBackBufferGameViewportPath())
    {
        return false;
    }

    if (IsUsingGameViewportSource())
    {
        if (!GEngine || !GEngine->GameViewport)
        {
            LogGameViewportFailure(TEXT("ResolveCurrentSource"), TEXT("GEngine->GameViewport is null."));
            return false;
        }

        FViewport* GameViewport = GEngine->GameViewport->Viewport;
        if (!GameViewport)
        {
            LogGameViewportFailure(
                TEXT("ResolveCurrentSource"),
                FString::Printf(
                    TEXT("GameViewportClient exists (%s) but Viewport is null."),
                    *GetNameSafe(GEngine->GameViewport)));
            return false;
        }

        FTextureRHIRef ViewportTexture = GameViewport->GetRenderTargetTexture();
        if (!ViewportTexture.IsValid())
        {
            LogGameViewportFailure(
                TEXT("ResolveCurrentSource"),
                FString::Printf(
                    TEXT("Viewport render target texture is invalid. Viewport=%p Size=%dx%d"),
                    GameViewport,
                    GameViewport->GetSizeXY().X,
                    GameViewport->GetSizeXY().Y));
            return false;
        }

        const FIntPoint Size = GameViewport->GetSizeXY();
        if (Size.X <= 0 || Size.Y <= 0)
        {
            LogGameViewportFailure(
                TEXT("ResolveCurrentSource"),
                FString::Printf(
                    TEXT("Viewport size is invalid (%d x %d). Viewport=%p"),
                    Size.X,
                    Size.Y,
                    GameViewport));
            return false;
        }

        OutTexture = ViewportTexture;
        OutWidth = Size.X;
        OutHeight = Size.Y;
        OutFormat = ViewportTexture->GetFormat();
        LogGameViewportReady(GameViewport, ViewportTexture, OutWidth, OutHeight, OutFormat);
        return true;
    }

    if (!CurrentRenderTarget)
    {
        return false;
    }

    FTextureRenderTargetResource* RTRes = CurrentRenderTarget->GameThread_GetRenderTargetResource();
    if (!RTRes)
    {
        return false;
    }

    FTextureRHIRef SourceTexture = RTRes->GetRenderTargetTexture();
    if (!SourceTexture.IsValid())
    {
        return false;
    }

    OutTexture = SourceTexture;
    OutWidth = CurrentRenderTarget->SizeX;
    OutHeight = CurrentRenderTarget->SizeY;
    OutFormat = CurrentRenderTarget->GetFormat();
    return true;
}

void USpoutSenderComponent::ResetGameViewportDebugState()
{
    LastGameViewportFailureKey.Reset();
    LastGameViewportFailureRepeatCount = 0;
    bHasLoggedGameViewportReady = false;
    LastLoggedGameViewportAddress = nullptr;
    LastLoggedGameViewportTextureAddress = nullptr;
    LastLoggedGameViewportWidth = 0;
    LastLoggedGameViewportHeight = 0;
    LastLoggedGameViewportFormat = PF_Unknown;
    GameViewportQueuedFrameCount = 0;
    GameViewportBackBufferReadyDelegateHandle.Reset();
    GameViewportWindow = nullptr;
    GameViewportRenderThreadContext.Reset();
    GameViewportMinSendIntervalSeconds = 0.0;
    GameViewportLastSendTimeSeconds = 0.0;
    bGameViewportBackBufferCallbackRegistered = false;
    bHasLoggedGameViewportBackBufferCallback = false;
    bHasLoggedGameViewportWrongWindowSkip = false;
    bHasLoggedGameViewportThrottle = false;
}

void USpoutSenderComponent::LogGameViewportFailure(const TCHAR* Context, const FString& Reason) const
{
#if PLATFORM_WINDOWS
    if (!IsUsingGameViewportSource())
    {
        return;
    }

    const FString FailureKey = FString::Printf(TEXT("%s|%s"), Context, *Reason);
    if (LastGameViewportFailureKey == FailureKey)
    {
        ++LastGameViewportFailureRepeatCount;
        if (LastGameViewportFailureRepeatCount % 120 != 0)
        {
            return;
        }

        UE_LOG(
            LogSpoutSender,
            Warning,
            TEXT("%s: %s (repeated %d times). %s"),
            Context,
            *Reason,
            LastGameViewportFailureRepeatCount + 1,
            *BuildSenderDebugContext(this));
        return;
    }

    LastGameViewportFailureKey = FailureKey;
    LastGameViewportFailureRepeatCount = 0;
    bHasLoggedGameViewportReady = false;

    UE_LOG(
        LogSpoutSender,
        Warning,
        TEXT("%s: %s %s"),
        Context,
        *Reason,
        *BuildSenderDebugContext(this));
#endif
}

void USpoutSenderComponent::LogGameViewportReady(
    const FViewport* Viewport,
    const FTextureRHIRef& ViewportTexture,
    int32 Width,
    int32 Height,
    EPixelFormat Format) const
{
#if PLATFORM_WINDOWS
    if (!IsUsingGameViewportSource())
    {
        return;
    }

    const void* ViewportAddress = Viewport;
    const void* TextureAddress = ViewportTexture.GetReference();

    if (bHasLoggedGameViewportReady &&
        LastLoggedGameViewportAddress == ViewportAddress &&
        LastLoggedGameViewportTextureAddress == TextureAddress &&
        LastLoggedGameViewportWidth == Width &&
        LastLoggedGameViewportHeight == Height &&
        LastLoggedGameViewportFormat == Format)
    {
        return;
    }

    bHasLoggedGameViewportReady = true;
    LastLoggedGameViewportAddress = ViewportAddress;
    LastLoggedGameViewportTextureAddress = TextureAddress;
    LastLoggedGameViewportWidth = Width;
    LastLoggedGameViewportHeight = Height;
    LastLoggedGameViewportFormat = Format;
    LastGameViewportFailureKey.Reset();
    LastGameViewportFailureRepeatCount = 0;

    UE_LOG(
        LogSpoutSender,
        Display,
        TEXT("ResolveCurrentSource: Game viewport texture is ready. Viewport=%p Texture=%p Size=%dx%d Format=%d. %s"),
        ViewportAddress,
        TextureAddress,
        Width,
        Height,
        static_cast<int32>(Format),
        *BuildSenderDebugContext(this));
#endif
}

void USpoutSenderComponent::EnsureBridge()
{
#if PLATFORM_WINDOWS
    if (!SpoutBridge)
    {
        SpoutBridge = new spoutDX12();
        const bool bOpened = SpoutBridge->OpenDirectX12();

        if (bOpened)
        {
            UE_LOG(
                LogSpoutSender,
                Display,
                TEXT("EnsureBridge: OpenDirectX12 succeeded. Bridge=%p. %s"),
                SpoutBridge,
                *BuildSenderDebugContext(this));
        }
        else
        {
            UE_LOG(
                LogSpoutSender,
                Error,
                TEXT("EnsureBridge: OpenDirectX12 failed. Bridge=%p. %s"),
                SpoutBridge,
                *BuildSenderDebugContext(this));
        }
    }
#endif
}

bool USpoutSenderComponent::RegisterGameViewportBackBufferCallback()
{
#if PLATFORM_WINDOWS
    if (!ShouldUseSlateBackBufferGameViewportPath())
    {
        return false;
    }

    if (!FSlateApplication::IsInitialized())
    {
        LogGameViewportFailure(TEXT("RegisterGameViewportBackBufferCallback"), TEXT("FSlateApplication is not initialized."));
        return false;
    }

    FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
    if (!Renderer)
    {
        LogGameViewportFailure(TEXT("RegisterGameViewportBackBufferCallback"), TEXT("Slate renderer is null."));
        return false;
    }

    if (!GEngine || !GEngine->GameViewport)
    {
        LogGameViewportFailure(TEXT("RegisterGameViewportBackBufferCallback"), TEXT("GEngine->GameViewport is null."));
        return false;
    }

    TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow();
    if (!Window.IsValid())
    {
        LogGameViewportFailure(TEXT("RegisterGameViewportBackBufferCallback"), TEXT("Game viewport window is invalid at sender start."));
        return false;
    }

    UnregisterGameViewportBackBufferCallback();

    GameViewportWindow = Window.Get();
    GameViewportRenderThreadContext = BuildSenderDebugContext(this);
    GameViewportBackBufferReadyDelegateHandle =
        Renderer->OnBackBufferReadyToPresent().AddWeakLambda(
            this,
            [this](SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer)
            {
                OnGameViewportBackBufferReady_RenderThread(SlateWindow, FrameBuffer);
            });
    bGameViewportBackBufferCallbackRegistered = true;

    UE_LOG(
        LogSpoutSender,
        Display,
        TEXT("RegisterGameViewportBackBufferCallback: Bound to window %p with min interval %.4f seconds. %s"),
        GameViewportWindow,
        GameViewportMinSendIntervalSeconds,
        *GameViewportRenderThreadContext);

    return true;
#else
    return false;
#endif
}

void USpoutSenderComponent::UnregisterGameViewportBackBufferCallback()
{
#if PLATFORM_WINDOWS
    if (!bGameViewportBackBufferCallbackRegistered)
    {
        return;
    }

    if (FSlateApplication::IsInitialized())
    {
        if (FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
        {
            Renderer->OnBackBufferReadyToPresent().Remove(GameViewportBackBufferReadyDelegateHandle);
        }
    }

    GameViewportBackBufferReadyDelegateHandle.Reset();
    bGameViewportBackBufferCallbackRegistered = false;
#endif
}

void USpoutSenderComponent::ShutdownBridge()
{
#if PLATFORM_WINDOWS
    FlushRenderingCommands();

    if (SpoutBridge)
    {
        if (IsUsingGameViewportSource())
        {
            UE_LOG(
                LogSpoutSender,
                Display,
                TEXT("ShutdownBridge: Closing bridge %p. %s"),
                SpoutBridge,
                *BuildSenderDebugContext(this));
        }

        SpoutBridge->CloseDirectX12();
        delete SpoutBridge;
        SpoutBridge = nullptr;
    }
#endif
}

void USpoutSenderComponent::OnGameViewportBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer)
{
#if PLATFORM_WINDOWS
    check(IsInRenderingThread());

    if (!bIsBroadcasting || !bGameViewportBackBufferCallbackRegistered)
    {
        return;
    }

    if (!SpoutBridge)
    {
        UE_LOG(LogSpoutSender, Error, TEXT("OnGameViewportBackBufferReady: SpoutBridge is null. %s"), *GameViewportRenderThreadContext);
        return;
    }

    if (GameViewportWindow == nullptr)
    {
        UE_LOG(
            LogSpoutSender,
            Warning,
            TEXT("OnGameViewportBackBufferReady: No cached game viewport window is bound for the callback path. %s"),
            *GameViewportRenderThreadContext);
        return;
    }

    if (&SlateWindow != GameViewportWindow)
    {
        if (!bHasLoggedGameViewportWrongWindowSkip)
        {
            bHasLoggedGameViewportWrongWindowSkip = true;
            UE_LOG(
                LogSpoutSender,
                Display,
                TEXT("OnGameViewportBackBufferReady: Ignoring backbuffer from non-game window %p (expected %p). %s"),
                &SlateWindow,
                GameViewportWindow,
                *GameViewportRenderThreadContext);
        }
        return;
    }

    if (!FrameBuffer.IsValid())
    {
        UE_LOG(LogSpoutSender, Error, TEXT("OnGameViewportBackBufferReady: FrameBuffer is invalid. %s"), *GameViewportRenderThreadContext);
        return;
    }

    if (!bHasLoggedGameViewportBackBufferCallback)
    {
        bHasLoggedGameViewportBackBufferCallback = true;
        UE_LOG(
            LogSpoutSender,
            Display,
            TEXT("OnGameViewportBackBufferReady: Matched game window backbuffer. Window=%p Texture=%p Size=%dx%d Format=%d. %s"),
            &SlateWindow,
            FrameBuffer.GetReference(),
            FrameBuffer->GetSizeX(),
            FrameBuffer->GetSizeY(),
            static_cast<int32>(FrameBuffer->GetFormat()),
            *GameViewportRenderThreadContext);
    }

    const double CurrentTimeSeconds = FPlatformTime::Seconds();
    if (GameViewportMinSendIntervalSeconds > 0.0 &&
        GameViewportLastSendTimeSeconds > 0.0 &&
        (CurrentTimeSeconds - GameViewportLastSendTimeSeconds) < GameViewportMinSendIntervalSeconds)
    {
        if (!bHasLoggedGameViewportThrottle)
        {
            bHasLoggedGameViewportThrottle = true;
            UE_LOG(
                LogSpoutSender,
                Display,
                TEXT("OnGameViewportBackBufferReady: Throttling callback-driven sends to %.4f seconds per frame. %s"),
                GameViewportMinSendIntervalSeconds,
                *GameViewportRenderThreadContext);
        }
        return;
    }

    const int32 SlotCount = bUseDoubleBuffer ? 2 : 1;
    const int32 SlotIndex = (SlotCount == 2) ? NextStageSlot : 0;
    NextStageSlot = (SlotIndex + 1) % SlotCount;

    FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
    const bool bSent = SendFrame_RenderThread(
        RHICmdList,
        FrameBuffer,
        FrameBuffer->GetSizeX(),
        FrameBuffer->GetSizeY(),
        FrameBuffer->GetFormat(),
        SlotIndex,
        ERHIAccess::SRVGraphics,
        ERHIAccess::SRVGraphics,
        true,
        true,
        GameViewportRenderThreadContext);

    if (!bSent)
    {
        return;
    }

    GameViewportLastSendTimeSeconds = CurrentTimeSeconds;
    ++GameViewportQueuedFrameCount;

    if (GameViewportQueuedFrameCount == 1)
    {
        UE_LOG(
            LogSpoutSender,
            Display,
            TEXT("OnGameViewportBackBufferReady: Sent first packaged game viewport frame on slot %d (%dx%d Format=%d DoubleBuffer=%s). %s"),
            SlotIndex,
            FrameBuffer->GetSizeX(),
            FrameBuffer->GetSizeY(),
            static_cast<int32>(FrameBuffer->GetFormat()),
            bUseDoubleBuffer ? TEXT("true") : TEXT("false"),
            *GameViewportRenderThreadContext);
    }
    else if (GameViewportQueuedFrameCount % 300 == 0)
    {
        UE_LOG(
            LogSpoutSender,
            Verbose,
            TEXT("OnGameViewportBackBufferReady: Sent %d packaged game viewport frames so far. Last slot=%d Size=%dx%d Format=%d. %s"),
            GameViewportQueuedFrameCount,
            SlotIndex,
            FrameBuffer->GetSizeX(),
            FrameBuffer->GetSizeY(),
            static_cast<int32>(FrameBuffer->GetFormat()),
            *GameViewportRenderThreadContext);
    }
#endif
}

void USpoutSenderComponent::StopBroadcastInternal(bool bClearConfiguration, bool bClearDesiredState)
{
    const bool bWasGameViewportSource = IsUsingGameViewportSource();

    bIsBroadcasting = false;
    SetComponentTickEnabled(false);
    SetComponentTickInterval(0.0f);
    ClearTickPrerequisite();
    ReleaseEditorOwnership();

#if PLATFORM_WINDOWS
    UnregisterGameViewportBackBufferCallback();
    FlushRenderingCommands();

    if (SpoutBridge)
    {
        SpoutBridge->ReleaseSender();
        SpoutBridge->SetSenderName("");
    }

    ResetStageSlots();
    ResetGameViewportDebugState();

    if (bWasGameViewportSource)
    {
        UE_LOG(
            LogSpoutSender,
            Display,
            TEXT("StopBroadcastInternal: Broadcast stopped. ClearConfiguration=%s ClearDesiredState=%s. %s"),
            bClearConfiguration ? TEXT("true") : TEXT("false"),
            bClearDesiredState ? TEXT("true") : TEXT("false"),
            *BuildSenderDebugContext(this));
    }

    if (bClearConfiguration)
    {
        CurrentRenderTarget = nullptr;
        CurrentSenderName.Empty();
        BroadcastFPS = 0;
    }

    if (bClearDesiredState)
    {
        bWantsBroadcasting = false;
        bBroadcastIntentInitialized = true;
    }

    NextStageSlot = 0;
#else
    if (bClearDesiredState)
    {
        bWantsBroadcasting = false;
    }
#endif
}

void USpoutSenderComponent::RefreshEditorState()
{
#if PLATFORM_WINDOWS
    if (!IsEditorWorld() && !IsPreviewWorld())
    {
        return;
    }

    StopBroadcastInternal(false, false);

    if (!IsSupportedWorld())
    {
        ShutdownBridge();
        return;
    }

    ApplyTickPrerequisite();

    if (!IsD3D12Active())
    {
        UE_LOG(LogSpoutSender, Warning,
            TEXT("SpoutSenderComponent: D3D12 RHI is not active (current RHI: %s). Editor sender is disabled."),
            GDynamicRHI ? *FString(GDynamicRHI->GetName()) : TEXT("None"));
        ShutdownBridge();
        return;
    }

    EnsureBridge();

    if (bWantsBroadcasting && HasValidConfiguredSource() && !CurrentSenderName.IsEmpty())
    {
        StartBroadcastConfigured(CurrentRenderTarget, CurrentSenderName, BroadcastFPS);
    }
#endif
}

void USpoutSenderComponent::InitializeDesiredState()
{
    if (!bBroadcastIntentInitialized)
    {
        bWantsBroadcasting = Auto_Start;
        bBroadcastIntentInitialized = true;
    }
}

bool USpoutSenderComponent::AcquireEditorOwnership(const FString& SenderName)
{
    if (!IsEditorWorld() && !IsPreviewWorld())
    {
        return true;
    }

    if (SenderName.IsEmpty())
    {
        return true;
    }

    for (TObjectIterator<USpoutSenderComponent> It; It; ++It)
    {
        USpoutSenderComponent* ExistingOwner = *It;
        if (!IsValid(ExistingOwner) || ExistingOwner == this || ExistingOwner->IsTemplate())
        {
            continue;
        }

        if (ExistingOwner->CurrentSenderName != SenderName)
        {
            continue;
        }

        if (!ExistingOwner->IsRegistered())
        {
            continue;
        }

        if (!ExistingOwner->GetWorld())
        {
            continue;
        }

        if (ExistingOwner->IsBeingDestroyed() || !ExistingOwner->bIsBroadcasting)
        {
            continue;
        }

        if (!ExistingOwner->IsEditorWorld() && !ExistingOwner->IsPreviewWorld())
        {
            continue;
        }

        if (!ExistingOwner->IsSupportedWorld())
        {
            ExistingOwner->StopBroadcastInternal(false, false);
            continue;
        }

        if (IsPreviewWorld())
        {
            return false;
        }

        if (ExistingOwner->IsPreviewWorld())
        {
            ExistingOwner->StopBroadcastInternal(false, false);
            continue;
        }

        return false;
    }

    return true;
}

void USpoutSenderComponent::ReleaseEditorOwnership()
{
}

void USpoutSenderComponent::ClearTickPrerequisite()
{
#if PLATFORM_WINDOWS
    if (AppliedTickAfterActor.IsValid())
    {
        RemoveTickPrerequisiteActor(AppliedTickAfterActor.Get());
        AppliedTickAfterActor.Reset();
    }
#endif
}

void USpoutSenderComponent::ApplyTickPrerequisite()
{
#if PLATFORM_WINDOWS
    ClearTickPrerequisite();

    if (TickAfterActor && TickAfterActor != GetOwner())
    {
        AddTickPrerequisiteActor(TickAfterActor);
        AppliedTickAfterActor = TickAfterActor;
    }
#endif
}

void USpoutSenderComponent::SetTickAfterActor(AActor* NewTickAfterActor)
{
#if PLATFORM_WINDOWS
    if (TickAfterActor == NewTickAfterActor)
    {
        return;
    }

    TickAfterActor = NewTickAfterActor;
    ApplyTickPrerequisite();

#if WITH_EDITOR
    if (IsEditorWorld() || IsPreviewWorld())
    {
        RefreshEditorState();
    }
#endif
#endif
}

void USpoutSenderComponent::OnRegister()
{
    Super::OnRegister();

#if PLATFORM_WINDOWS
    if (!IsSupportedWorld())
    {
        return;
    }

    InitializeDesiredState();

    if (!IsEditorWorld() && !IsPreviewWorld())
    {
        return;
    }

    ApplyTickPrerequisite();

    if (!IsD3D12Active())
    {
        return;
    }

    EnsureBridge();

    if (bWantsBroadcasting && HasValidConfiguredSource() && !CurrentSenderName.IsEmpty())
    {
        StartBroadcastConfigured(CurrentRenderTarget, CurrentSenderName, BroadcastFPS);
    }
#endif
}

void USpoutSenderComponent::OnUnregister()
{
#if PLATFORM_WINDOWS
    StopBroadcastInternal(false, false);
    ClearTickPrerequisite();
    ShutdownBridge();
#endif

    Super::OnUnregister();
}

void USpoutSenderComponent::BeginPlay()
{
    Super::BeginPlay();

#if PLATFORM_WINDOWS
    if (IsUsingGameViewportSource())
    {
        UE_LOG(
            LogSpoutSender,
            Display,
            TEXT("BeginPlay: Entered runtime sender initialization. AutoStart=%s DesiredBroadcast=%s SenderName='%s' FPS=%d DoubleBuffer=%s. %s"),
            Auto_Start ? TEXT("true") : TEXT("false"),
            bWantsBroadcasting ? TEXT("true") : TEXT("false"),
            *CurrentSenderName,
            BroadcastFPS,
            bUseDoubleBuffer ? TEXT("true") : TEXT("false"),
            *BuildSenderDebugContext(this));
    }

    if (!IsSupportedWorld() || IsEditorWorld() || IsPreviewWorld())
    {
        if (IsUsingGameViewportSource())
        {
            LogGameViewportFailure(TEXT("BeginPlay"), TEXT("World is not a supported runtime game world."));
        }
        return;
    }

    if (!IsD3D12Active())
    {
        UE_LOG(LogSpoutSender, Warning,
            TEXT("SpoutSenderComponent: D3D12 RHI is not active (current RHI: %s). Spout DX12 sender is disabled."),
            GDynamicRHI ? *FString(GDynamicRHI->GetName()) : TEXT("None"));
        return;
    }

    InitializeDesiredState();
    EnsureBridge();
    ApplyTickPrerequisite();

    if (bWantsBroadcasting && HasValidConfiguredSource() && !CurrentSenderName.IsEmpty())
    {
        StartBroadcastConfigured(CurrentRenderTarget, CurrentSenderName, BroadcastFPS);
    }
#endif
}

void USpoutSenderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if PLATFORM_WINDOWS
    if (!bIsBroadcasting)
    {
        return;
    }

    UpdateTexture();
#endif
}

void USpoutSenderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopBroadcastInternal(false, false);
    ClearTickPrerequisite();

#if PLATFORM_WINDOWS
    ShutdownBridge();
#endif

    Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void USpoutSenderComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

#if PLATFORM_WINDOWS
    const FName PropertyName = PropertyChangedEvent.GetPropertyName();

    if (PropertyName == GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, Auto_Start))
    {
        if (!bIsBroadcasting)
        {
            bWantsBroadcasting = Auto_Start;
        }

        bBroadcastIntentInitialized = true;
    }

    if (PropertyName == GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, Auto_Start) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, CurrentSenderName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, CurrentRenderTarget) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, BroadcastFPS) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, bUseDoubleBuffer) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, SourceType) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, StartupPolicy) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, TickAfterActor))
    {
        RefreshEditorState();
    }
#endif
}
#endif

bool USpoutSenderComponent::SendFrame_RenderThread(
    FRHICommandListImmediate& RHICmdList,
    const FTextureRHIRef& SrcRHI,
    int32 W,
    int32 H,
    EPixelFormat PF,
    int32 SlotIndex,
    ERHIAccess SourceBeforeAccess,
    ERHIAccess SourceAfterAccess,
    bool bRestoreSourceState,
    bool bLogGameViewport,
    const FString& SenderContext)
{
#if PLATFORM_WINDOWS
    if (!SpoutBridge)
    {
        if (bLogGameViewport)
        {
            UE_LOG(LogSpoutSender, Error, TEXT("SendFrame_RenderThread: SpoutBridge is null. %s"), *SenderContext);
        }
        return false;
    }

    if (!SrcRHI.IsValid())
    {
        if (bLogGameViewport)
        {
            UE_LOG(LogSpoutSender, Error, TEXT("SendFrame_RenderThread: Source texture is invalid. Slot=%d Size=%dx%d Format=%d. %s"), SlotIndex, W, H, static_cast<int32>(PF), *SenderContext);
        }
        return false;
    }

    if (SlotIndex < 0 || SlotIndex > 1)
    {
        if (bLogGameViewport)
        {
            UE_LOG(LogSpoutSender, Error, TEXT("SendFrame_RenderThread: SlotIndex %d is invalid. %s"), SlotIndex, *SenderContext);
        }
        return false;
    }

    FSpoutStageSlot& Slot = StageSlots[SlotIndex];

    const bool bNeedCreate =
        !Slot.Texture.IsValid() ||
        Slot.Width != W ||
        Slot.Height != H ||
        Slot.Format != PF;

    if (bNeedCreate)
    {
        if (Slot.Wrapped11)
        {
            Slot.Wrapped11->Release();
            Slot.Wrapped11 = nullptr;
        }

        Slot.Texture.SafeRelease();

        FRHITextureCreateDesc Desc =
            FRHITextureCreateDesc::Create2D(TEXT("SpoutStagingShared"), W, H, PF)
            .SetFlags(ETextureCreateFlags::ShaderResource |
                ETextureCreateFlags::RenderTargetable |
                ETextureCreateFlags::Shared);

        Slot.Texture = RHICreateTexture(Desc);
        Slot.Width = W;
        Slot.Height = H;
        Slot.Format = PF;

        if (bLogGameViewport)
        {
            UE_LOG(
                LogSpoutSender,
                Display,
                TEXT("SendFrame_RenderThread: Created or resized staging texture for slot %d to %dx%d Format=%d."),
                SlotIndex,
                W,
                H,
                static_cast<int32>(PF));
        }
    }

    if (!Slot.Texture.IsValid())
    {
        if (bLogGameViewport)
        {
            UE_LOG(LogSpoutSender, Error, TEXT("SendFrame_RenderThread: Failed to create staging texture for slot %d. %s"), SlotIndex, *SenderContext);
        }
        return false;
    }

    FRHITexture* Src = SrcRHI.GetReference();
    FRHITexture* Dst = Slot.Texture.GetReference();

    const ERHIAccess DstBefore = bNeedCreate ? ERHIAccess::Unknown : ERHIAccess::SRVMask;

    RHICmdList.Transition(FRHITransitionInfo(Src, SourceBeforeAccess, ERHIAccess::CopySrc));
    RHICmdList.Transition(FRHITransitionInfo(Dst, DstBefore, ERHIAccess::CopyDest));

    FRHICopyTextureInfo CopyInfo;
    RHICmdList.CopyTexture(Src, Dst, CopyInfo);

    if (bRestoreSourceState)
    {
        RHICmdList.Transition(FRHITransitionInfo(Src, ERHIAccess::CopySrc, SourceAfterAccess));
    }

    RHICmdList.Transition(FRHITransitionInfo(Dst, ERHIAccess::CopyDest, ERHIAccess::SRVMask));

    if (!Slot.Wrapped11)
    {
        ID3D12Resource* NativeDX12 = static_cast<ID3D12Resource*>(Slot.Texture->GetNativeResource());
        if (!NativeDX12)
        {
            if (bLogGameViewport)
            {
                UE_LOG(LogSpoutSender, Error, TEXT("SendFrame_RenderThread: Native DX12 resource is null for slot %d. %s"), SlotIndex, *SenderContext);
            }
            return false;
        }

        if (!SpoutBridge->WrapDX12Resource(NativeDX12, &Slot.Wrapped11, D3D12_RESOURCE_STATE_GENERIC_READ) ||
            !Slot.Wrapped11)
        {
            if (bLogGameViewport)
            {
                UE_LOG(LogSpoutSender, Error, TEXT("SendFrame_RenderThread: WrapDX12Resource failed for slot %d NativeDX12=%p. %s"), SlotIndex, NativeDX12, *SenderContext);
            }
            return false;
        }
    }

    const bool bSent = SpoutBridge->SendDX11Resource(Slot.Wrapped11);
    if (!bSent)
    {
        if (bLogGameViewport)
        {
            UE_LOG(LogSpoutSender, Error, TEXT("SendFrame_RenderThread: SendDX11Resource failed for slot %d Wrapped11=%p. %s"), SlotIndex, Slot.Wrapped11, *SenderContext);
        }
        return false;
    }

    if (bLogGameViewport && bNeedCreate)
    {
        UE_LOG(
            LogSpoutSender,
            Display,
            TEXT("SendFrame_RenderThread: First send succeeded for slot %d (%dx%d Format=%d Wrapped11=%p). %s"),
            SlotIndex,
            W,
            H,
            static_cast<int32>(PF),
            Slot.Wrapped11,
            *SenderContext);
    }
    return true;
#endif
    return false;
}

void USpoutSenderComponent::QueueSendFrame_RenderThread(FTextureRHIRef SrcRHI, int32 W, int32 H, EPixelFormat PF, int32 SlotIndex)
{
#if PLATFORM_WINDOWS
    const bool bLogGameViewport = IsUsingGameViewportSource();
    const FString SenderContext = bLogGameViewport ? BuildSenderDebugContext(this) : FString();

    ENQUEUE_RENDER_COMMAND(SpoutSendFrame)(
        [this, SrcRHI, W, H, PF, SlotIndex, bLogGameViewport, SenderContext](FRHICommandListImmediate& RHICmdList)
        {
            SendFrame_RenderThread(
                RHICmdList,
                SrcRHI,
                W,
                H,
                PF,
                SlotIndex,
                ERHIAccess::RTV,
                ERHIAccess::RTV,
                false,
                bLogGameViewport,
                SenderContext);
        });
#endif
}

void USpoutSenderComponent::ResetStageSlots()
{
#if PLATFORM_WINDOWS
    for (int32 i = 0; i < 2; ++i)
    {
        StageSlots[i].Texture.SafeRelease();

        if (StageSlots[i].Wrapped11)
        {
            StageSlots[i].Wrapped11->Release();
            StageSlots[i].Wrapped11 = nullptr;
        }

        StageSlots[i].Width = 0;
        StageSlots[i].Height = 0;
        StageSlots[i].Format = PF_Unknown;
    }

    NextStageSlot = 0;
#endif
}

void USpoutSenderComponent::UpdateTexture()
{
#if PLATFORM_WINDOWS
    if (ShouldUseSlateBackBufferGameViewportPath())
    {
        return;
    }

    if (!SpoutBridge || !bIsBroadcasting)
    {
        if (IsUsingGameViewportSource() && bIsBroadcasting && !SpoutBridge)
        {
            LogGameViewportFailure(TEXT("UpdateTexture"), TEXT("Broadcasting is active but SpoutBridge is null."));
        }
        return;
    }

    FTextureRHIRef SrcRHI;
    int32 W = 0;
    int32 H = 0;
    EPixelFormat PF = PF_Unknown;

    if (!ResolveCurrentSource(SrcRHI, W, H, PF))
    {
        return;
    }

    const int32 SlotCount = bUseDoubleBuffer ? 2 : 1;

    int32 SlotIndex = NextStageSlot;

    if (!StageSlots[SlotIndex].Fence.IsFenceComplete())
    {
        if (SlotCount == 2)
        {
            const int32 OtherSlot = 1 - SlotIndex;

            if (StageSlots[OtherSlot].Fence.IsFenceComplete())
            {
                if (IsUsingGameViewportSource())
                {
                    UE_LOG(
                        LogSpoutSender,
                        Verbose,
                        TEXT("UpdateTexture: Stage slot %d still busy, switching to slot %d. %s"),
                        SlotIndex,
                        OtherSlot,
                        *BuildSenderDebugContext(this));
                }
                SlotIndex = OtherSlot;
            }
            else
            {
                LogGameViewportFailure(TEXT("UpdateTexture"), TEXT("Both stage-slot fences are still pending; frame send skipped."));
                return;
            }
        }
        else
        {
            LogGameViewportFailure(TEXT("UpdateTexture"), TEXT("Single-buffer stage slot fence is still pending; frame send skipped."));
            return;
        }
    }

    QueueSendFrame_RenderThread(SrcRHI, W, H, PF, SlotIndex);

    StageSlots[SlotIndex].Fence.BeginFence();

    NextStageSlot = (SlotIndex + 1) % SlotCount;

    if (IsUsingGameViewportSource())
    {
        ++GameViewportQueuedFrameCount;

        if (GameViewportQueuedFrameCount == 1)
        {
            UE_LOG(
                LogSpoutSender,
                Display,
                TEXT("UpdateTexture: Queued first game viewport frame on slot %d (%dx%d Format=%d DoubleBuffer=%s). %s"),
                SlotIndex,
                W,
                H,
                static_cast<int32>(PF),
                bUseDoubleBuffer ? TEXT("true") : TEXT("false"),
                *BuildSenderDebugContext(this));
        }
        else if (GameViewportQueuedFrameCount % 300 == 0)
        {
            UE_LOG(
                LogSpoutSender,
                Verbose,
                TEXT("UpdateTexture: Queued %d game viewport frames so far. Last slot=%d Size=%dx%d Format=%d. %s"),
                GameViewportQueuedFrameCount,
                SlotIndex,
                W,
                H,
                static_cast<int32>(PF),
                *BuildSenderDebugContext(this));
        }
    }
#endif
}

void USpoutSenderComponent::StartBroadcastConfigured(
    UTextureRenderTarget2D* RenderTarget,
    const FString& SenderName,
    int32 FPS)
{
#if PLATFORM_WINDOWS
    bWantsBroadcasting = true;
    bBroadcastIntentInitialized = true;

    if (bIsBroadcasting)
    {
        StopBroadcastInternal(false, false);
    }

    if (!IsSupportedWorld())
    {
        if (IsUsingGameViewportSource())
        {
            LogGameViewportFailure(TEXT("StartBroadcastConfigured"), TEXT("Current world is not supported for runtime broadcasting."));
        }
        return;
    }

    if (!IsD3D12Active())
    {
        UE_LOG(LogSpoutSender, Warning,
            TEXT("SpoutSenderComponent: D3D12 RHI is not active (current RHI: %s). Spout DX12 sender is disabled."),
            GDynamicRHI ? *FString(GDynamicRHI->GetName()) : TEXT("None"));
        return;
    }

    EnsureBridge();

    if (!SpoutBridge || SenderName.IsEmpty())
    {
        if (IsUsingGameViewportSource())
        {
            LogGameViewportFailure(
                TEXT("StartBroadcastConfigured"),
                SenderName.IsEmpty() ? TEXT("Sender name is empty.") : TEXT("SpoutBridge is null after EnsureBridge."));
        }
        return;
    }

    if (SourceType == ESpoutSenderSourceType::RenderTarget && !RenderTarget)
    {
        return;
    }

    if (!AcquireEditorOwnership(SenderName))
    {
        return;
    }

    CurrentRenderTarget = (SourceType == ESpoutSenderSourceType::RenderTarget) ? RenderTarget : nullptr;
    CurrentSenderName = SenderName;
    BroadcastFPS = FPS;
    bIsBroadcasting = true;
    ResetGameViewportDebugState();

    const bool bSetSenderName = SpoutBridge->SetSenderName(TCHAR_TO_ANSI(*CurrentSenderName));

    if (IsUsingGameViewportSource())
    {
        if (bSetSenderName)
        {
            UE_LOG(
                LogSpoutSender,
                Display,
                TEXT("StartBroadcastConfigured: SetSenderName(%s) succeeded. FPS=%d DoubleBuffer=%s Bridge=%p."),
                *CurrentSenderName,
                BroadcastFPS,
                bUseDoubleBuffer ? TEXT("true") : TEXT("false"),
                SpoutBridge);
        }
        else
        {
            UE_LOG(
                LogSpoutSender,
                Error,
                TEXT("StartBroadcastConfigured: SetSenderName(%s) failed. FPS=%d DoubleBuffer=%s Bridge=%p."),
                *CurrentSenderName,
                BroadcastFPS,
                bUseDoubleBuffer ? TEXT("true") : TEXT("false"),
                SpoutBridge);
        }
    }

    ApplyTickPrerequisite();

    const bool bUseSlateBackBufferCallback = ShouldUseSlateBackBufferGameViewportPath();
    GameViewportMinSendIntervalSeconds =
        (BroadcastFPS > 0)
        ? (1.0 / static_cast<double>(FMath::Clamp(BroadcastFPS, 1, 240)))
        : 0.0;

    if (bUseSlateBackBufferCallback)
    {
        SetComponentTickInterval(0.0f);
        SetComponentTickEnabled(false);

        if (!RegisterGameViewportBackBufferCallback())
        {
            StopBroadcastInternal(false, false);
            return;
        }

        UE_LOG(
            LogSpoutSender,
            Display,
            TEXT("StartBroadcastConfigured: Using Slate backbuffer callback for packaged game viewport capture. Min interval %.4f seconds. %s"),
            GameViewportMinSendIntervalSeconds,
            *GameViewportRenderThreadContext);
        return;
    }

    if (BroadcastFPS > 0)
    {
        SetComponentTickInterval(1.0f / static_cast<float>(FMath::Clamp(BroadcastFPS, 1, 240)));
    }
    else
    {
        SetComponentTickInterval(0.0f);
    }

    SetComponentTickEnabled(true);

    if (IsUsingGameViewportSource())
    {
        UE_LOG(
            LogSpoutSender,
            Display,
            TEXT("StartBroadcastConfigured: Tick enabled with interval %.4f seconds. %s"),
            PrimaryComponentTick.TickInterval,
            *BuildSenderDebugContext(this));
    }

    UpdateTexture();
#endif
}

void USpoutSenderComponent::StartBroadcastFromRenderTarget(
    UTextureRenderTarget2D* RenderTarget,
    const FString& SenderName,
    int32 FPS,
    bool bEnableDoubleBuffer)
{
#if PLATFORM_WINDOWS
    SourceType = ESpoutSenderSourceType::RenderTarget;
    bUseDoubleBuffer = bEnableDoubleBuffer;
    StartupPolicy = ESpoutWorldBootstrapPolicy::GameOnly;

    StartBroadcastConfigured(RenderTarget, SenderName, FPS);
#endif
}

void USpoutSenderComponent::StartBroadcastGameViewport(
    const FString& SenderName,
    int32 FPS,
    bool bEnableDoubleBuffer)
{
#if PLATFORM_WINDOWS
    SourceType = ESpoutSenderSourceType::GameViewport;
    bUseDoubleBuffer = bEnableDoubleBuffer;
    StartupPolicy = ESpoutWorldBootstrapPolicy::GameOnly;

    UE_LOG(
        LogSpoutSender,
        Display,
        TEXT("StartBroadcastGameViewport: Requested sender start. Sender='%s' FPS=%d DoubleBuffer=%s WorldType=%s Owner='%s'."),
        *SenderName,
        FPS,
        bEnableDoubleBuffer ? TEXT("true") : TEXT("false"),
        GetSenderWorldTypeName(GetWorld()),
        *GetNameSafe(GetOwner()));

    StartBroadcastConfigured(nullptr, SenderName, FPS);
#endif
}

void USpoutSenderComponent::StartBroadcast()
{
#if PLATFORM_WINDOWS
    StartBroadcastConfigured(CurrentRenderTarget, CurrentSenderName, BroadcastFPS);
#endif
}

void USpoutSenderComponent::StopBroadcast()
{
    StopBroadcastInternal(true, true);
}

void USpoutSenderComponent::ChangeRenderTarget(UTextureRenderTarget2D* NewRenderTarget)
{
#if PLATFORM_WINDOWS
    if (NewRenderTarget == CurrentRenderTarget)
    {
        return;
    }

    FlushRenderingCommands();

    CurrentRenderTarget = NewRenderTarget;
    ResetStageSlots();

    if (bIsBroadcasting && CurrentRenderTarget)
    {
        UpdateTexture();
    }
#endif
}
