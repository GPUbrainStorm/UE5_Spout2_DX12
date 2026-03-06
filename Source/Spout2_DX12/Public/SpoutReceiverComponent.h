#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "RHIResources.h"
#include "SpoutReceiverComponent.generated.h"

// Forward declare Spout classes
class spoutDX;
class spoutDX12;
class FTextureRenderTargetResource;

UCLASS(ClassGroup = (Spout), meta = (BlueprintSpawnableComponent))
class USpoutReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpoutReceiverComponent();
	virtual ~USpoutReceiverComponent() override;


	// Auto Start receiving on BeginPlay
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout Receiver")
	bool bAutoStart = true;

	// Output render target
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout Receiver")
	TObjectPtr<UTextureRenderTarget2D> OutputRenderTarget;

	// Target FPS for receiving (0 = receive once then stop)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout Receiver")
	int32 TargetFPS = 60;

	// Specific sender name to connect to (empty = first available)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout Receiver")
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout Receiver")
	bool bUseDoubleBuffer = false;

	// Internal render targets for receiving and copying (when using stable user RT option).
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> InternalRT_A;
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> InternalRT_B;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// Spout objects
	spoutDX* SpoutInfo = nullptr;
	spoutDX12* SpoutDX12 = nullptr;
	// NEW: cache which RT resource we wrapped (wrap only when RT resource changes/recreates)
	// MUST be pointers (so you can assign nullptr / compare pointers)
	FTextureRenderTargetResource* CachedRTRes[2] = { nullptr, nullptr };
	UTextureRenderTarget2D* CachedRTObject[2] = { nullptr, nullptr };
	int32 InternalWriteIndex = 0;     // 0 or 1
	int32 InternalReadyIndex = -1;    // -1 until first frame is written
	bool  bSeededOutput = false;      // true after first successful publish to user RT

	// Incoming sender info + resources
	struct FIncoming
	{
		void* WrappedDest11[2] = { nullptr, nullptr }; // ID3D11Resource*
		void* GPUCopy11 = nullptr;                    // ID3D11Texture2D*

		// REQUIRED (your .cpp uses these)
		void* CachedSrc11 = nullptr;                  // ID3D11Texture2D*
		void* CachedShareHandle = nullptr;            // HANDLE stored as void*

		uint32 Width = 0;
		uint32 Height = 0;
		uint32 Format = 87; // DXGI_FORMAT_B8G8R8A8_UNORM
	} Incoming;

	// Time vars
	float TickAccumulator = 0.f;
	float TargetInterval = 1.0f / 60.0f;

	// State flags
	bool bReceiving = false;
	bool bConnected = false;

	// init and release functions
	bool InitSpoutDevices();
	void ReleaseSpoutDevices();

	// Receive and copy to UE render target
	bool ReceiveOnce();
	bool EnsureGpuRenderTarget(uint32 W, uint32 H, int32 Index, UTextureRenderTarget2D* TargetRT);
	bool EnsureUserOutputRT(uint32 W, uint32 H);
	bool EnsureInternalRTs(uint32 W, uint32 H);

	// DX11 and DX12 RHI devices
	static struct ID3D12Device* GetUE_D3D12Device();
	static struct ID3D11On12Device* GetD3D11On12(spoutDX12* InDX12);

	// Thread safety lock
	mutable FCriticalSection SpoutLock;
};