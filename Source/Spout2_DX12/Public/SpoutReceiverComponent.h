#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "RHIResources.h"
#include "HAL/PlatformTime.h"
#include "SpoutWorldPolicy.h"
#include "SpoutReceiverComponent.generated.h"

#if PLATFORM_WINDOWS
struct ID3D11Device5;
struct ID3D11DeviceContext4;
struct ID3D11Fence;
#endif

// Forward declare Spout classes
class spoutDX;
class spoutDX12;
class FTextureRenderTargetResource;

#if WITH_EDITOR
struct FPropertyChangedEvent;
#endif

UCLASS(ClassGroup = (Spout), meta = (BlueprintSpawnableComponent))
class USpoutReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpoutReceiverComponent();
	virtual ~USpoutReceiverComponent() override;


	// Auto Start receiving on BeginPlay
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout Receiver", meta = (ToolTip = "If enabled, the receiver starts automatically when the component becomes active in a supported world."))
	bool bAutoStart = false;

	// Output render target
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout Receiver", meta = (ToolTip = "The render target that received frames are copied into for use by materials, UI, or gameplay logic."))
	TObjectPtr<UTextureRenderTarget2D> OutputRenderTarget;

	// Target FPS for receiving (0 = receive once then stop)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout Receiver", meta = (ToolTip = "How often the receiver pulls frames from Spout. Set to 0 to receive one frame and stop automatically."))
	int32 TargetFPS = 60;

	// Specific sender name to connect to (empty = first available)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout Receiver", meta = (ToolTip = "The specific Spout sender name to connect to. Leave empty to connect to the first available sender."))
	FString SpoutSenderName;

	// Manually start the receiver
	UFUNCTION(BlueprintCallable, Category = "Spout")
	void StartReceiving();

	// Manually stop the receiver
	UFUNCTION(BlueprintCallable, Category = "Spout")
	void StopReceiving();

	// Get a list of available Spout senders
	UFUNCTION(BlueprintCallable, Category = "Spout")
	TArray<FString> GetAvailableSenders() const;

	// Check if currently connected to a sender
	UFUNCTION(BlueprintPure, Category = "Spout")
	bool IsConnected() const { return bConnected; }

	// Keep OutputRenderTarget stable for users; use internal buffers for receiving.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout Receiver", meta = (ToolTip = "Uses internal double buffering so the output render target stays stable while new frames are copied in. This can improve smoothness at the cost of memory and latency."))
	bool bUseDoubleBuffer = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout Receiver", meta = (ToolTip = "Controls which world types are allowed to auto-start or editor-start this receiver component."))
	ESpoutWorldBootstrapPolicy StartupPolicy = ESpoutWorldBootstrapPolicy::GameOnly;

	// Internal render targets for receiving and copying (when using stable user RT option).
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> InternalRT_A;
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> InternalRT_B;

	// Debug / runtime stats
	UPROPERTY(BlueprintReadOnly, Category = "Spout Receiver|Stats", meta = (ToolTip = "Average number of copy operations completed per second over the current stats window."))
	float CopiesPerSecond = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Spout Receiver|Stats", meta = (ToolTip = "Average number of first-stage flush operations per second over the current stats window."))
	float Flush1PerSecond = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Spout Receiver|Stats", meta = (ToolTip = "Average number of flush operations per second over the current stats window."))
	float FlushPerSecond = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Spout Receiver|Stats", meta = (ToolTip = "How many times the receiver had to reconnect to the sender during this session."))
	int32 ReconnectCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Spout Receiver|Stats", meta = (ToolTip = "How many frames were missed while receiving during this session."))
	int32 MissedFrames = 0;

protected:
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    bool IsEditorWorld() const;
    bool IsPreviewWorld() const;
    bool IsSupportedWorld() const;
    bool IsD3D12Active() const;
    void RecomputeTargetInterval();
    void RefreshEditorState();
    void InitializeDesiredState();
    void StopReceivingInternal(bool bClearDesiredState);

	// Spout objects
	spoutDX* SpoutInfo = nullptr;
	spoutDX12* SpoutDX12 = nullptr;
	FTextureRenderTargetResource* CachedRTRes[2] = { nullptr, nullptr };
	UTextureRenderTarget2D* CachedRTObject[2] = { nullptr, nullptr };
	int32 InternalWriteIndex = 0;     // 0 or 1

	// stats internals
	uint64 StatCopyCount = 0;
	uint64 StatFlush1Count = 0;
	uint64 StatFlushCount = 0;
	uint64 StatMissedFramesAccum = 0;
	double StatWindowStartSeconds = 0.0;

	// Incoming sender info + resources
	struct FIncoming
	{
		void* WrappedDest11[2] = { nullptr, nullptr }; 
		void* GPUCopy11 = nullptr;                    

		void* CachedSrc11 = nullptr;                 
		void* CachedShareHandle = nullptr;            

		uint32 Width = 0;
		uint32 Height = 0;
		uint32 Format = 87; // DXGI_FORMAT_B8G8R8A8_UNORM
	} Incoming;

	#if PLATFORM_WINDOWS
		struct FCopyFenceSlotState
		{
			uint64 FenceValue = 0;
			bool bPendingPublish = false;
		};
	#endif

	// Time vars
	float TickAccumulator = 0.f;
	float TargetInterval = 1.0f / 60.0f;

	// State flags
	bool bReceiving = false;
	bool bConnected = false;
    bool bWantsReceiving = false;
    bool bReceiveIntentInitialized = false;

	// init and release functions
	bool InitSpoutDevices();
	void ReleaseSpoutDevices();

	// Receive and copy to UE render target
	bool ReceiveOnce();
	bool EnsureGpuRenderTarget(uint32 W, uint32 H, int32 Index, UTextureRenderTarget2D* TargetRT);
	bool EnsureUserOutputRT(uint32 W, uint32 H);
	bool EnsureInternalRTs(uint32 W, uint32 H);
	void ResetStats();
	void UpdateStatsWindow();
	bool CacheDX11Context3();

	// fence sync for GPU work submission and completion tracking
	bool CacheDX11FenceObjects();
	void ReleaseFenceObjects();
	bool SignalSubmittedWork(int32 TrackedSlotIndex);
	void PublishCompletedInternalBuffer();

	#if PLATFORM_WINDOWS
		ID3D11Device5* CachedDev11_5 = nullptr;
		ID3D11DeviceContext4* CachedCtx11_4 = nullptr;
		ID3D11Fence* CopyFence11 = nullptr;

		uint64 NextFenceValue = 1;
		uint64 LastPublishedFenceValue = 0;

		FCopyFenceSlotState SlotFenceState[2];
	#endif

	bool bPendingOneShotStop = false;

	// DX11 and DX12 RHI devices
	static struct ID3D12Device* GetUE_D3D12Device();
	static struct ID3D11On12Device* GetD3D11On12(spoutDX12* InDX12);

	// Thread safety lock
	mutable FCriticalSection SpoutLock;
};
