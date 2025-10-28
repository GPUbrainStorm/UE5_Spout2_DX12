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
#include "Windows/MinWindows.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

class FTextureRenderTargetResource;

USpoutSenderComponent::USpoutSenderComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void USpoutSenderComponent::BeginPlay()
{
    Super::BeginPlay();
    SpoutBridge.OpenDirectX12();
    if (Auto_Start) {
        StartBroadcast(CurrentRenderTarget, CurrentSenderName, BroadcastFPS);
    }
}

void USpoutSenderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    SpoutBridge.CloseDirectX12();
    Super::EndPlay(EndPlayReason);
}

void USpoutSenderComponent::UpdateTexture()
{
#if PLATFORM_WINDOWS
    if (!CurrentRenderTarget) return;

    // Get source RT RHI
    FTextureRenderTargetResource* RTRes = CurrentRenderTarget->GameThread_GetRenderTargetResource();
    if (!RTRes) return;
    FTextureRHIRef SrcRHI = RTRes->GetRenderTargetTexture();
    if (!SrcRHI.IsValid()) return;

    // Create a shared staging texture matching the RT
    const int32 W = CurrentRenderTarget->SizeX;
    const int32 H = CurrentRenderTarget->SizeY;
    const EPixelFormat PF = CurrentRenderTarget->GetFormat();

	// Check if we need to (re)create the staging texture
    const bool NeedCreate = (!StagingRHI.IsValid() || W != StagingW || H != StagingH || PF != StagingPF);
    if (NeedCreate)
    {
        StagingRHI.SafeRelease();
        StagingWrapped11 = nullptr;

        FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("SpoutStagingShared"), W, H, PF)
            .SetFlags(ETextureCreateFlags::ShaderResource    // readable for copy/send
                | ETextureCreateFlags::RenderTargetable  // some RHIs require this for CopyTexture path
                | ETextureCreateFlags::Shared);          // shared so D3D11 side can read

        StagingRHI = RHICreateTexture(Desc);
        StagingW = W; StagingH = H; StagingPF = PF;
    }

    // Copy RT to the staging on the render thread
    FTextureRHIRef DstRHI = StagingRHI;
    ENQUEUE_RENDER_COMMAND(SpoutCopyRTToStaging)(
        [SrcRHI, DstRHI](FRHICommandListImmediate& RHICmdList)
        {
        FRHITexture* Src = SrcRHI.GetReference();
        FRHITexture* Dst = DstRHI.GetReference();

        RHICmdList.Transition(FRHITransitionInfo(Src, ERHIAccess::RTV, ERHIAccess::CopySrc));
        RHICmdList.Transition(FRHITransitionInfo(Dst, ERHIAccess::Unknown, ERHIAccess::CopyDest));

        FRHICopyTextureInfo Info;
        RHICmdList.CopyTexture(Src, Dst, Info);

        RHICmdList.Transition(FRHITransitionInfo(Dst, ERHIAccess::CopyDest, ERHIAccess::SRVMask));

        //RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
        });

	// Ensure the copy is done before proceeding
    FlushRenderingCommands();

	// Wrap the staging texture once and set the sender name
    if (!StagingWrapped11)
    {
        ID3D12Resource* StagingDX12 = static_cast<ID3D12Resource*>(StagingRHI->GetNativeResource());
        if (!StagingDX12) return;

        if (!SpoutBridge.WrapDX12Resource(StagingDX12, &StagingWrapped11, D3D12_RESOURCE_STATE_GENERIC_READ) || !StagingWrapped11)
        {
            return;
        }
    }

	// Send the copied staging texture to Spout
    SpoutBridge.SendDX11Resource(StagingWrapped11);
#endif
}

void USpoutSenderComponent::StartBroadcast(UTextureRenderTarget2D* RenderTarget, const FString& SenderName, int32 FPS)
{
    if (!RenderTarget || SenderName.IsEmpty()) return;

    CurrentRenderTarget = RenderTarget;
    CurrentSenderName = SenderName;
    BroadcastFPS = FPS;

	// Set sender name
    spoutDX12* LocalSpoutBridge = &SpoutBridge;
    LocalSpoutBridge->SetSenderName(TCHAR_TO_ANSI(*CurrentSenderName));

    // Send once
    UpdateTexture();

	if (FPS <= 0) return;

    float Interval = 1.0f / FMath::Clamp(FPS, 1, 240);

    UWorld* World = GetWorld();
    if (!World) return;

    World->GetTimerManager().SetTimer(BroadcastTimerHandle, this, &USpoutSenderComponent::UpdateTexture, Interval, true);
}

void USpoutSenderComponent::StopBroadcast()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BroadcastTimerHandle);
		CurrWrappedResource->Release();
		CurrWrappedResource = nullptr;
    }
}