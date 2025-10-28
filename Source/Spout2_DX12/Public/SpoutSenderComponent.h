#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpoutDX12.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "RHIResources.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "SpoutSenderComponent.generated.h"

UCLASS(ClassGroup = (Spout), meta = (BlueprintSpawnableComponent))
class SPOUT2_DX12_API USpoutSenderComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USpoutSenderComponent();

    UFUNCTION(BlueprintCallable, Category = "Spout")

	void UpdateTexture();

    UFUNCTION(BlueprintCallable, Category = "Spout")
    void StartBroadcast(UTextureRenderTarget2D* RenderTarget, const FString& SenderName = "Sender Component", int32 FPS = 60);

    UFUNCTION(BlueprintCallable, Category = "Spout")
    void StopBroadcast();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
    bool Auto_Start = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
    FString CurrentSenderName = "Component Broadcast";
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
    UTextureRenderTarget2D* CurrentRenderTarget = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
    int32 BroadcastFPS = 60;

    FTimerHandle BroadcastTimerHandle;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	spoutDX12 SpoutBridge; // Spout DX12 wrapper
	ID3D11Resource* CurrWrappedResource = nullptr; // wrapped live texture
	ID3D11Resource* StagingWrapped11 = nullptr; // wrapped staging texture for stable readback
	FTextureRHIRef StagingRHI; // staging texture RHI
	int32 StagingW = 0, StagingH = 0; // staging texture size
	EPixelFormat StagingPF = PF_Unknown; // staging texture format
};