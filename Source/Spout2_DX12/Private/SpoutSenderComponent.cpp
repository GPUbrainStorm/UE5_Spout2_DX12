#include "SpoutSenderComponent.h"

#include "Engine/World.h"
#include "TimerManager.h"
#include "Engine/TextureRenderTarget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIResources.h"
#include "RHICommandList.h"
#include "RenderResource.h"
#include "TextureResource.h"
#include "Logging/LogMacros.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"

THIRD_PARTY_INCLUDES_START
#include "Windows/AllowWindowsPlatformTypes.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

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

#include "Windows/HideWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_END
#endif


USpoutSenderComponent::USpoutSenderComponent()
{
    PrimaryComponentTick.bCanEverTick = false; // Timer drives updates.
}

void USpoutSenderComponent::BeginPlay()
{
    Super::BeginPlay();

#if PLATFORM_WINDOWS  
    // Run only on D3D12.
    if (!(GDynamicRHI && FString(GDynamicRHI->GetName()) == TEXT("D3D12")))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("SpoutSenderComponent: D3D12 RHI is not active (current RHI: %s). Spout DX12 sender is disabled."),
            GDynamicRHI ? *FString(GDynamicRHI->GetName()) : TEXT("None"));
        return;
    }

    if (!SpoutBridge)
    {
        SpoutBridge = new spoutDX12();
    }

    SpoutBridge->OpenDirectX12();

    if (Auto_Start && CurrentRenderTarget && !CurrentSenderName.IsEmpty())
    {
        StartBroadcast(CurrentRenderTarget, CurrentSenderName, BroadcastFPS);
    }
#endif
}

void USpoutSenderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopBroadcast();

#if PLATFORM_WINDOWS  
    // Wait for queued render work that may still use SpoutBridge.
    FlushRenderingCommands();

    if (SpoutBridge)
    {
        SpoutBridge->CloseDirectX12();
        delete SpoutBridge;
        SpoutBridge = nullptr;
    }
#endif

    Super::EndPlay(EndPlayReason);
}

void USpoutSenderComponent::UpdateTexture()
{
#if PLATFORM_WINDOWS  
    if (!SpoutBridge || !CurrentRenderTarget)
        return;

    // Game thread: get source render target resource.
    FTextureRenderTargetResource* RTRes = CurrentRenderTarget->GameThread_GetRenderTargetResource();
    if (!RTRes)
        return;

    FTextureRHIRef SrcRHI = RTRes->GetRenderTargetTexture();
    if (!SrcRHI.IsValid())
        return;

    const int32 W = CurrentRenderTarget->SizeX;
    const int32 H = CurrentRenderTarget->SizeY;
    const EPixelFormat PF = CurrentRenderTarget->GetFormat();

    // Recreate staging texture when size or format changes.
    const bool NeedCreate = (!StagingRHI.IsValid() || W != StagingW || H != StagingH || PF != StagingPF);
    if (NeedCreate)
    {
        StagingRHI.SafeRelease();
        if (StagingWrapped11)
        {
            StagingWrapped11->Release();
            StagingWrapped11 = nullptr;
        }

        FRHITextureCreateDesc Desc =
            FRHITextureCreateDesc::Create2D(TEXT("SpoutStagingShared"), W, H, PF)
            .SetFlags(ETextureCreateFlags::ShaderResource |
                ETextureCreateFlags::RenderTargetable |
                ETextureCreateFlags::Shared);

        FRHITextureCreateDesc LocalDesc = Desc;

        ENQUEUE_RENDER_COMMAND(CreateSpoutStagingShared)(
            [this, LocalDesc](FRHICommandListImmediate& RHICmdList)
        {
            StagingRHI.SafeRelease();
            StagingRHI = RHICreateTexture(LocalDesc);
        });

        // Wait until the render thread creates the texture.
        FlushRenderingCommands();
        StagingW = W;
        StagingH = H;
        StagingPF = PF;
    }

    if (!StagingRHI.IsValid())
        return;

    FTextureRHIRef LocalSrc = SrcRHI;
    FTextureRHIRef LocalDst = StagingRHI;

    // Queue a copy and wait until it finishes.
    FGraphEventRef CopyDoneEvent = FFunctionGraphTask::CreateAndDispatchWhenReady(
        [LocalSrc, LocalDst]()
    {
        ENQUEUE_RENDER_COMMAND(SpoutCopyRT)(
            [LocalSrc, LocalDst](FRHICommandListImmediate& RHICmdList)
        {
            FRHITexture* Src = LocalSrc.GetReference();
            FRHITexture* Dst = LocalDst.GetReference();

            RHICmdList.Transition(FRHITransitionInfo(Src, ERHIAccess::RTV, ERHIAccess::CopySrc));
            RHICmdList.Transition(FRHITransitionInfo(Dst, ERHIAccess::Unknown, ERHIAccess::CopyDest));

            FRHICopyTextureInfo Info;
            RHICmdList.CopyTexture(Src, Dst, Info);

            RHICmdList.Transition(FRHITransitionInfo(Dst, ERHIAccess::CopyDest, ERHIAccess::SRVMask));
        });
    },
        TStatId(),
        nullptr,
        ENamedThreads::AnyBackgroundThreadNormalTask
    );

    // Wait for the render-thread copy.
    CopyDoneEvent->Wait();

    // After copy, wrap and send on the game thread.
    if (!StagingWrapped11)
    {
        ID3D12Resource* NativeDX12 = (ID3D12Resource*)StagingRHI->GetNativeResource();
        if (!NativeDX12)
            return;

        if (!SpoutBridge->WrapDX12Resource(NativeDX12, &StagingWrapped11, D3D12_RESOURCE_STATE_GENERIC_READ) ||
            !StagingWrapped11)
            return;
    }

    SpoutBridge->SendDX11Resource(StagingWrapped11);
#endif
}

void USpoutSenderComponent::StartBroadcast(
    UTextureRenderTarget2D* RenderTarget,
    const FString& SenderName,
    int32 FPS)
{
#if PLATFORM_WINDOWS  
    if (!SpoutBridge || !RenderTarget || SenderName.IsEmpty())
    {
        return;
    }

    CurrentRenderTarget = RenderTarget;
    CurrentSenderName = SenderName;
    BroadcastFPS = FPS;

    SpoutBridge->SetSenderName(TCHAR_TO_ANSI(*CurrentSenderName));

    // Send one frame now.
    UpdateTexture();

    if (FPS <= 0)
    {
        return;
    }

    const float Interval = 1.0f / FMath::Clamp(FPS, 1, 240);

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            BroadcastTimerHandle,
            this,
            &USpoutSenderComponent::UpdateTexture,
            Interval,
            true);
    }
#endif
}

void USpoutSenderComponent::StopBroadcast()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BroadcastTimerHandle);
    }

#if PLATFORM_WINDOWS  
    // Wait so no render-thread work still uses these textures.
    FlushRenderingCommands();

    if (SpoutBridge)
    {
        // Release current sender resources.
        SpoutBridge->ReleaseSender();
        SpoutBridge->SetSenderName(""); // optional: clear name explicitly
    }

    StagingRHI.SafeRelease();
    if (StagingWrapped11)
    {
        StagingWrapped11->Release();
        StagingWrapped11 = nullptr;
    }
    StagingW = 0;
    StagingH = 0;
    StagingPF = PF_Unknown;

    CurrentRenderTarget = nullptr;
    CurrentSenderName.Empty();
    BroadcastFPS = 0;
#endif
}

// Change render target at runtime.
void USpoutSenderComponent::ChangeRenderTarget(UTextureRenderTarget2D* NewRenderTarget)
{
#if PLATFORM_WINDOWS  
    if (NewRenderTarget == CurrentRenderTarget)
    {
        return;
    }

    CurrentRenderTarget = NewRenderTarget;

    // Force staging texture recreate on next UpdateTexture().
    StagingRHI.SafeRelease();
    if (StagingWrapped11)
    {
        StagingWrapped11->Release();
        StagingWrapped11 = nullptr;
    }
    StagingW = 0;
    StagingH = 0;
    StagingPF = PF_Unknown;

    // If broadcasting, send one frame now.
    if (BroadcastFPS > 0 && CurrentRenderTarget)
    {
        UpdateTexture();
    }
#endif
}