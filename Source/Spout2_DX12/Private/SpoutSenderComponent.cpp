#include "SpoutSenderComponent.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/Actor.h"
#include "Engine/TextureRenderTarget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RHICommandList.h"
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
    static bool IsSenderEditorWorld(const UWorld* World)
    {
        return World && World->WorldType == EWorldType::Editor;
    }

    static bool IsSenderPreviewWorld(const UWorld* World)
    {
        return World && World->WorldType == EWorldType::EditorPreview;
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

    if (IsUsingGameViewportSource())
    {
        if (!GEngine || !GEngine->GameViewport)
        {
            return false;
        }

        FViewport* GameViewport = GEngine->GameViewport->Viewport;
        if (!GameViewport)
        {
            return false;
        }

        FTextureRHIRef ViewportTexture = GameViewport->GetRenderTargetTexture();
        if (!ViewportTexture.IsValid())
        {
            return false;
        }

        const FIntPoint Size = GameViewport->GetSizeXY();
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

void USpoutSenderComponent::EnsureBridge()
{
#if PLATFORM_WINDOWS
    if (!SpoutBridge)
    {
        SpoutBridge = new spoutDX12();
        SpoutBridge->OpenDirectX12();
    }
#endif
}

void USpoutSenderComponent::ShutdownBridge()
{
#if PLATFORM_WINDOWS
    FlushRenderingCommands();

    if (SpoutBridge)
    {
        SpoutBridge->CloseDirectX12();
        delete SpoutBridge;
        SpoutBridge = nullptr;
    }
#endif
}

void USpoutSenderComponent::StopBroadcastInternal(bool bClearConfiguration, bool bClearDesiredState)
{
    bIsBroadcasting = false;
    SetComponentTickEnabled(false);
    SetComponentTickInterval(0.0f);
    ClearTickPrerequisite();
    ReleaseEditorOwnership();

#if PLATFORM_WINDOWS
    FlushRenderingCommands();

    if (SpoutBridge)
    {
        SpoutBridge->ReleaseSender();
        SpoutBridge->SetSenderName("");
    }

    ResetStageSlots();

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
        UE_LOG(LogTemp, Warning,
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
    if (!IsSupportedWorld() || IsEditorWorld() || IsPreviewWorld())
    {
        return;
    }

    if (!IsD3D12Active())
    {
        UE_LOG(LogTemp, Warning,
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

void USpoutSenderComponent::QueueSendFrame_RenderThread(FTextureRHIRef SrcRHI, int32 W, int32 H, EPixelFormat PF, int32 SlotIndex)
{
#if PLATFORM_WINDOWS
    ENQUEUE_RENDER_COMMAND(SpoutSendFrame)(
        [this, SrcRHI, W, H, PF, SlotIndex](FRHICommandListImmediate& RHICmdList)
        {
            if (!SpoutBridge || !SrcRHI.IsValid() || SlotIndex < 0 || SlotIndex > 1)
            {
                return;
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
            }

            if (!Slot.Texture.IsValid())
            {
                return;
            }

            FRHITexture* Src = SrcRHI.GetReference();
            FRHITexture* Dst = Slot.Texture.GetReference();

            const ERHIAccess DstBefore = bNeedCreate ? ERHIAccess::Unknown : ERHIAccess::SRVMask;

            RHICmdList.Transition(FRHITransitionInfo(Src, ERHIAccess::RTV, ERHIAccess::CopySrc));
            RHICmdList.Transition(FRHITransitionInfo(Dst, DstBefore, ERHIAccess::CopyDest));

            FRHICopyTextureInfo CopyInfo;
            RHICmdList.CopyTexture(Src, Dst, CopyInfo);

            RHICmdList.Transition(FRHITransitionInfo(Dst, ERHIAccess::CopyDest, ERHIAccess::SRVMask));

            if (!Slot.Wrapped11)
            {
                ID3D12Resource* NativeDX12 = static_cast<ID3D12Resource*>(Slot.Texture->GetNativeResource());
                if (!NativeDX12)
                {
                    return;
                }

                if (!SpoutBridge->WrapDX12Resource(NativeDX12, &Slot.Wrapped11, D3D12_RESOURCE_STATE_GENERIC_READ) ||
                    !Slot.Wrapped11)
                {
                    return;
                }
            }

            SpoutBridge->SendDX11Resource(Slot.Wrapped11);
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
    if (!SpoutBridge || !bIsBroadcasting)
    {
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
                SlotIndex = OtherSlot;
            }
            else
            {
                return;
            }
        }
        else
        {
            return;
        }
    }

    QueueSendFrame_RenderThread(SrcRHI, W, H, PF, SlotIndex);

    StageSlots[SlotIndex].Fence.BeginFence();

    NextStageSlot = (SlotIndex + 1) % SlotCount;
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
        return;
    }

    if (!IsD3D12Active())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("SpoutSenderComponent: D3D12 RHI is not active (current RHI: %s). Spout DX12 sender is disabled."),
            GDynamicRHI ? *FString(GDynamicRHI->GetName()) : TEXT("None"));
        return;
    }

    EnsureBridge();

    if (!SpoutBridge || SenderName.IsEmpty())
    {
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

    SpoutBridge->SetSenderName(TCHAR_TO_ANSI(*CurrentSenderName));

    ApplyTickPrerequisite();

    if (BroadcastFPS > 0)
    {
        SetComponentTickInterval(1.0f / static_cast<float>(FMath::Clamp(BroadcastFPS, 1, 240)));
    }
    else
    {
        SetComponentTickInterval(0.0f);
    }

    SetComponentTickEnabled(true);

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
