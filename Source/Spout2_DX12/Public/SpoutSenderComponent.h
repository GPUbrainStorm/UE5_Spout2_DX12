#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpoutDX12.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "RHIResources.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "SpoutSenderComponent.generated.h"

// Forward declare Spout classes
class spoutDX;
class spoutDX12;

UCLASS(ClassGroup = (Spout), meta = (BlueprintSpawnableComponent))
class SPOUT2_DX12_API USpoutSenderComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USpoutSenderComponent();

	void UpdateTexture();

    UFUNCTION(BlueprintCallable, Category = "Spout")
    void StartBroadcast(UTextureRenderTarget2D* RenderTarget, const FString& SenderName = "Sender Component", int32 FPS = 60);

    UFUNCTION(BlueprintCallable, Category = "Spout")
    void StopBroadcast();

    UFUNCTION(BlueprintCallable, Category = "Spout")
    void ChangeRenderTarget(UTextureRenderTarget2D* NewRenderTarget);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
    bool Auto_Start = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
    FString CurrentSenderName = "Broadcast Component";
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
    UTextureRenderTarget2D* CurrentRenderTarget = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
    int32 BroadcastFPS = 60;

    FTimerHandle BroadcastTimerHandle;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // SpoutSenderComponent.cpp
    spoutDX12* SpoutBridge = nullptr;
	ID3D11Resource* CurrWrappedResource = nullptr;
	ID3D11Resource* StagingWrapped11 = nullptr;
	FTextureRHIRef StagingRHI;
	int32 StagingW = 0, StagingH = 0;
	EPixelFormat StagingPF = PF_Unknown;
};