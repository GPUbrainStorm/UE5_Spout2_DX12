#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RHIResources.h"
#include "RenderCommandFence.h"
#include "SpoutSenderComponent.generated.h"

struct ID3D11Resource;

// Forward declare Spout classes
class spoutDX;
class spoutDX12;
class AActor;

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
    bool bUseDoubleBuffer = false;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Spout")
    TObjectPtr<AActor> TickAfterActor = nullptr;

    UFUNCTION(BlueprintCallable, Category = "Spout")
    void SetTickAfterActor(AActor* NewTickAfterActor);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    struct FSpoutStageSlot
    {
        FTextureRHIRef Texture;
        ID3D11Resource* Wrapped11 = nullptr;
        int32 Width = 0;
        int32 Height = 0;
        EPixelFormat Format = PF_Unknown;
        FRenderCommandFence Fence;
    };

    void ApplyTickPrerequisite();
    void ClearTickPrerequisite();

    TWeakObjectPtr<AActor> AppliedTickAfterActor;

    spoutDX12* SpoutBridge = nullptr;

    FSpoutStageSlot StageSlots[2];
    int32 NextStageSlot = 0;

    bool bIsBroadcasting = false;

    void ResetStageSlots();
    void QueueSendFrame_RenderThread(FTextureRHIRef SrcRHI, int32 W, int32 H, EPixelFormat PF, int32 SlotIndex);
};