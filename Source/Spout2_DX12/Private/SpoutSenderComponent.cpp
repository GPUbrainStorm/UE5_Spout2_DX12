#include "SpoutSenderComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RHI.h"
#include "Spout2BlueprintLibrary.h"
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpoutDX12.h"

USpoutSenderComponent::USpoutSenderComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void USpoutSenderComponent::BeginPlay()
{
    Super::BeginPlay();
    SpoutBridge.OpenDirectX12();
    StartBroadcast(CurrentRenderTarget, CurrentSenderName, BroadcastFPS);

}

void USpoutSenderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    SpoutBridge.CloseDirectX12();
    Super::EndPlay(EndPlayReason);
}

void USpoutSenderComponent::SendRenderTarget(UTextureRenderTarget2D* RenderTarget, const FString& SenderName)
{
#if PLATFORM_WINDOWS
    if (!RenderTarget || SenderName.IsEmpty()) return;

    FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
    if (!RTResource) return;

    FTextureRHIRef RHITexture = RTResource->GetRenderTargetTexture();
    if (!RHITexture.IsValid()) return;

    spoutDX12* LocalSpoutBridge = &SpoutBridge;

    ENQUEUE_RENDER_COMMAND(SendSpoutRenderTarget)(
        [RHITexture, LocalSpoutBridge, SenderName](FRHICommandListImmediate& RHICmdList)
        {
            ID3D12Resource* NativeTexture = static_cast<ID3D12Resource*>(RHITexture->GetNativeResource());
            if (!NativeTexture || !LocalSpoutBridge) return;

            LocalSpoutBridge->SetSenderName(TCHAR_TO_ANSI(*SenderName));

            ID3D11Resource* Wrapped11Resource = nullptr;
            if (LocalSpoutBridge->WrapDX12Resource(NativeTexture, &Wrapped11Resource, D3D12_RESOURCE_STATE_RENDER_TARGET) && Wrapped11Resource)
            {
                LocalSpoutBridge->SendDX11Resource(Wrapped11Resource);
                Wrapped11Resource->Release();
            }
        });
#endif
}


void USpoutSenderComponent::BroadcastTick()
{
    if (CurrentRenderTarget && !CurrentSenderName.IsEmpty())
    {
        SendRenderTarget(CurrentRenderTarget, CurrentSenderName);
    }
}

void USpoutSenderComponent::StartBroadcast(UTextureRenderTarget2D* RenderTarget, const FString& SenderName, int32 FPS)
{
    if (!RenderTarget || SenderName.IsEmpty()) return;

    CurrentRenderTarget = RenderTarget;
    CurrentSenderName = SenderName;
    BroadcastFPS = FPS;

    // Immediate send once
    SendRenderTarget(CurrentRenderTarget, CurrentSenderName);

	if (FPS <= 0) return;

    float Interval = 1.0f / FMath::Clamp(FPS, 1, 240);

    UWorld* World = GetWorld();
    if (!World) return;

    World->GetTimerManager().SetTimer(BroadcastTimerHandle, this, &USpoutSenderComponent::BroadcastTick, Interval, true);
}

void USpoutSenderComponent::StopBroadcast()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BroadcastTimerHandle);
    }
}