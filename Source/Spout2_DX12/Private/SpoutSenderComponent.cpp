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

#if PLATFORM_WINDOWS
#include "Windows/MinWindows.h"

THIRD_PARTY_INCLUDES_START
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi.h>
#include "Windows/HideWindowsPlatformTypes.h"

#include "SpoutDX.h"
#include "SpoutDX12.h"
THIRD_PARTY_INCLUDES_END
#endif

USpoutSenderComponent::USpoutSenderComponent()
{
    PrimaryComponentTick.bCanEverTick = false; // driven by timer
}

void USpoutSenderComponent::BeginPlay()
{
    Super::BeginPlay();

#if PLATFORM_WINDOWS
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
    // Make sure all queued render commands that may use SpoutBridge are done
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

    // Game thread: Get source RT resource
    FTextureRenderTargetResource* RTRes = CurrentRenderTarget->GameThread_GetRenderTargetResource();
    if (!RTRes)
        return;

    FTextureRHIRef SrcRHI = RTRes->GetRenderTargetTexture();
    if (!SrcRHI.IsValid())
        return;

    const int32 W = CurrentRenderTarget->SizeX;
    const int32 H = CurrentRenderTarget->SizeY;
    const EPixelFormat PF = CurrentRenderTarget->GetFormat();

    // Recreate staging RHI texture if needed
    const bool NeedCreate = (!StagingRHI.IsValid() || W != StagingW || H != StagingH || PF != StagingPF);
    if (NeedCreate)
    {
        StagingRHI.SafeRelease();
        StagingWrapped11 = nullptr;

        FRHITextureCreateDesc Desc =
            FRHITextureCreateDesc::Create2D(TEXT("SpoutStagingShared"), W, H, PF)
            .SetFlags(ETextureCreateFlags::ShaderResource |
                ETextureCreateFlags::RenderTargetable |
                ETextureCreateFlags::Shared);

        StagingRHI = RHICreateTexture(Desc);
        StagingW = W;
        StagingH = H;
        StagingPF = PF;
    }

    if (!StagingRHI.IsValid())
        return;

    FTextureRHIRef LocalSrc = SrcRHI;
    FTextureRHIRef LocalDst = StagingRHI;

    // Create a GPU fence to wait for copy to complete
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

    // Game thread waits for render-thread copy without flushing whole command buffer
    CopyDoneEvent->Wait();

    // Now safe to wrap and send on GAME THREAD
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

    // Send one frame immediately
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
    StagingRHI.SafeRelease();
    StagingW = 0;
    StagingH = 0;
    StagingPF = PF_Unknown;

    CurrentRenderTarget = nullptr;
    CurrentSenderName.Empty();
#endif
}