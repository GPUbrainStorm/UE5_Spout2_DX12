#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RHIResources.h"
#include "RenderCommandFence.h"
#include "SpoutSenderSource.h"
#include "SpoutWorldPolicy.h"
#include "SpoutSenderComponent.generated.h"

struct ID3D11Resource;

class spoutDX;
class spoutDX12;
class AActor;

#if WITH_EDITOR
struct FPropertyChangedEvent;
#endif

UCLASS(ClassGroup = (Spout), meta = (BlueprintSpawnableComponent))
class SPOUT2_DX12_API USpoutSenderComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USpoutSenderComponent();

	void UpdateTexture();

    UFUNCTION(BlueprintCallable, Category = "Spout")
    void StartBroadcastFromRenderTarget(
        UTextureRenderTarget2D* RenderTarget,
        const FString& SenderName = "Sender Component",
        int32 FPS = 60,
        bool bEnableDoubleBuffer = false);

    UFUNCTION(BlueprintCallable, Category = "Spout")
    void StartBroadcastGameViewport(
        const FString& SenderName = "Sender Component",
        int32 FPS = 60,
        bool bEnableDoubleBuffer = false);

    UFUNCTION(BlueprintCallable, Category = "Spout")
    void StartBroadcast();

    UFUNCTION(BlueprintCallable, Category = "Spout")
    void StopBroadcast();

    UFUNCTION(BlueprintCallable, Category = "Spout")
    void ChangeRenderTarget(UTextureRenderTarget2D* NewRenderTarget);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout", meta = (ToolTip = "If enabled, the sender starts automatically when the component becomes active in a supported world."))
    bool Auto_Start = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout", meta = (ToolTip = "The published Spout sender name. Receivers use this name to find and connect to this sender."))
    FString CurrentSenderName = "Broadcast Component";
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout", meta = (ToolTip = "Selects which Unreal source this component sends to Spout: a render target, the game viewport, or the editor viewport."))
    ESpoutSenderSourceType SourceType = ESpoutSenderSourceType::RenderTarget;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout", meta = (EditCondition = "SourceType == ESpoutSenderSourceType::RenderTarget", EditConditionHides, ToolTip = "The render target to send when Source Type is set to Render Target."))
    UTextureRenderTarget2D* CurrentRenderTarget = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout", meta = (ToolTip = "How often the sender pushes frames. Set to 0 to disable throttling and tick every frame."))
    int32 BroadcastFPS = 60;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout", meta = (ToolTip = "Uses two staging buffers instead of one. This can improve frame pacing at the cost of more GPU memory and latency."))
    bool bUseDoubleBuffer = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout", meta = (ToolTip = "Controls which world types are allowed to auto-start or editor-start this sender component. Runtime Blueprint start helpers can override this internally."))
    ESpoutWorldBootstrapPolicy StartupPolicy = ESpoutWorldBootstrapPolicy::GameOnly;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Spout", meta = (ToolTip = "Optional actor that this component should tick after. Use this when the source texture is updated by another actor earlier in the frame."))
    TObjectPtr<AActor> TickAfterActor = nullptr;

    UFUNCTION(BlueprintCallable, Category = "Spout")
    void SetTickAfterActor(AActor* NewTickAfterActor);

protected:
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

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

    bool IsEditorWorld() const;
    bool IsPreviewWorld() const;
    bool IsSupportedWorld() const;
    bool IsD3D12Active() const;
    bool IsUsingEditorViewportSource() const;
    bool IsUsingGameViewportSource() const;
    bool HasValidConfiguredSource() const;
    bool ResolveCurrentSource(FTextureRHIRef& OutTexture, int32& OutWidth, int32& OutHeight, EPixelFormat& OutFormat) const;

    void EnsureBridge();
    void ShutdownBridge();
    void RefreshEditorState();
    void StopBroadcastInternal(bool bClearConfiguration, bool bClearDesiredState);
    void StartBroadcastConfigured(UTextureRenderTarget2D* RenderTarget, const FString& SenderName, int32 FPS);
    void InitializeDesiredState();
    bool AcquireEditorOwnership(const FString& SenderName);
    void ReleaseEditorOwnership();

    void ApplyTickPrerequisite();
    void ClearTickPrerequisite();

    TWeakObjectPtr<AActor> AppliedTickAfterActor;

    spoutDX12* SpoutBridge = nullptr;

    FSpoutStageSlot StageSlots[2];
    int32 NextStageSlot = 0;

    bool bIsBroadcasting = false;
    bool bWantsBroadcasting = false;
    bool bBroadcastIntentInitialized = false;

    void ResetStageSlots();
    void QueueSendFrame_RenderThread(FTextureRHIRef SrcRHI, int32 W, int32 H, EPixelFormat PF, int32 SlotIndex);
};
