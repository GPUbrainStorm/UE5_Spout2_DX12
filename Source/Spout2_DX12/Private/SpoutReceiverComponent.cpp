#include "SpoutReceiverComponent.h"
#include "RHI.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeLock.h"
#include "Spout2_DX12.h"
#include "Engine/TextureRenderTarget.h"
#include "RenderingThread.h"
#include "RenderCommandFence.h"
#include "RHICommandList.h"
#include "TextureResource.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

#if PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Windows/WindowsHWrapper.h"

THIRD_PARTY_INCLUDES_START
#include "Windows/AllowWindowsPlatformTypes.h"

#include <wrl/client.h>  

#include <d3d11.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi.h>

#include "SpoutDX.h"
#include "SpoutDX12.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#include <d3d11_4.h>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#ifdef IsMinimized
#undef IsMinimized
#endif

#include "Windows/HideWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_END
#endif

using Microsoft::WRL::ComPtr;

static EPixelFormat MapDxgiToUE(DXGI_FORMAT fmt) {
	switch (fmt) {
	// 8-bit Integer Formats
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return PF_B8G8R8A8;

	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return PF_R8G8B8A8;

	// 16-bit Float Formats
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		return PF_FloatRGBA;

	// 32-bit Float Formats
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
		return PF_A32B32G32R32F;

	// 10-bit Formats
	case DXGI_FORMAT_R10G10B10A2_UNORM:
		return PF_A2B10G10R10;

	// Fallback for unknown or unsupported formats
	default:
		UE_LOG(LogSpoutRX, Warning, TEXT("MapDxgiToUE: Unsupported DXGI_FORMAT %u, falling back to PF_B8G8R8A8."), (uint32)fmt);
		return PF_B8G8R8A8;
	}
}

// Check if format is sRGB
static bool IsDXGISRGB(DXGI_FORMAT f) {
	return f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
		f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}

namespace
{
	static bool IsReceiverEditorWorld(const UWorld* World)
	{
		return World && World->WorldType == EWorldType::Editor;
	}

	static bool IsReceiverPreviewWorld(const UWorld* World)
	{
		return World && World->WorldType == EWorldType::EditorPreview;
	}
}

// Constructor / destructor.
USpoutReceiverComponent::USpoutReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.0f;
	bTickInEditor = true;
	ResetStats();
}

USpoutReceiverComponent::~USpoutReceiverComponent()
{
	StopReceiving();
	ReleaseSpoutDevices();
}

bool USpoutReceiverComponent::IsEditorWorld() const
{
	return IsReceiverEditorWorld(GetWorld());
}

bool USpoutReceiverComponent::IsPreviewWorld() const
{
	return IsReceiverPreviewWorld(GetWorld());
}

bool USpoutReceiverComponent::IsSupportedWorld() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (World->IsGameWorld())
	{
		return true;
	}

	switch (StartupPolicy)
	{
	case ESpoutWorldBootstrapPolicy::GameOnly:
		return false;
	case ESpoutWorldBootstrapPolicy::EditorAndGame:
		return IsReceiverEditorWorld(World);
	case ESpoutWorldBootstrapPolicy::EditorGameAndSinglePreview:
		return IsReceiverEditorWorld(World) || IsReceiverPreviewWorld(World);
	default:
		return false;
	}
}

bool USpoutReceiverComponent::IsD3D12Active() const
{
	return GDynamicRHI && FString(GDynamicRHI->GetName()) == TEXT("D3D12");
}

void USpoutReceiverComponent::RecomputeTargetInterval()
{
	if (TargetFPS > 0)
	{
		TargetFPS = FMath::Clamp(TargetFPS, 1, 240);
		TargetInterval = 1.0f / static_cast<float>(TargetFPS);
	}
	else
	{
		TargetInterval = 0.0f;
	}

	TickAccumulator = 0.0f;
}

void USpoutReceiverComponent::RefreshEditorState()
{
#if PLATFORM_WINDOWS
	if ((!IsEditorWorld() && !IsPreviewWorld()) || !IsSupportedWorld())
	{
		return;
	}

	StopReceivingInternal(false);
	ReleaseSpoutDevices();
	ResetStats();
	RecomputeTargetInterval();

	if (!IsD3D12Active())
	{
		UE_LOG(LogSpoutRX, Warning,
			TEXT("SpoutReceiverComponent: D3D12 RHI is not active (current RHI: %s). Editor receiver is disabled."),
			GDynamicRHI ? *FString(GDynamicRHI->GetName()) : TEXT("None"));
		return;
	}

	InitSpoutDevices();

	if (bWantsReceiving && OutputRenderTarget)
	{
		StartReceiving();
	}
#endif
}

void USpoutReceiverComponent::InitializeDesiredState()
{
	if (!bReceiveIntentInitialized)
	{
		bWantsReceiving = bAutoStart;
		bReceiveIntentInitialized = true;
	}
}

void USpoutReceiverComponent::OnRegister()
{
	Super::OnRegister();

#if PLATFORM_WINDOWS
	if (!IsSupportedWorld())
	{
		return;
	}

	InitializeDesiredState();

	ResetStats();
	RecomputeTargetInterval();

	if (!IsEditorWorld() && !IsPreviewWorld())
	{
		return;
	}

	if (!IsD3D12Active())
	{
		return;
	}

	InitSpoutDevices();

	if (bWantsReceiving && OutputRenderTarget)
	{
		StartReceiving();
	}
#endif
}

void USpoutReceiverComponent::OnUnregister()
{
#if PLATFORM_WINDOWS
	StopReceivingInternal(false);
	ReleaseSpoutDevices();
#endif

	Super::OnUnregister();
}

#if WITH_EDITOR
void USpoutReceiverComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

#if PLATFORM_WINDOWS
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(USpoutReceiverComponent, bAutoStart))
	{
		if (!bReceiving)
		{
			bWantsReceiving = bAutoStart;
		}

		bReceiveIntentInitialized = true;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(USpoutReceiverComponent, bAutoStart) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(USpoutReceiverComponent, OutputRenderTarget) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(USpoutReceiverComponent, TargetFPS) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(USpoutReceiverComponent, SpoutSenderName) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(USpoutReceiverComponent, bUseDoubleBuffer) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(USpoutReceiverComponent, StartupPolicy))
	{
		RefreshEditorState();
	}
#endif
}
#endif

// Set interval, init Spout, and auto-start if enabled.
void USpoutReceiverComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!IsSupportedWorld() || IsEditorWorld() || IsPreviewWorld())
	{
		return;
	}

#if PLATFORM_WINDOWS
	if (!IsD3D12Active())
	{
		UE_LOG(LogSpoutRX, Warning,
			TEXT("SpoutReceiverComponent: D3D12 RHI is not active (current RHI: %s). Receiver is disabled."),
			GDynamicRHI ? *FString(GDynamicRHI->GetName()) : TEXT("None"));
		return;
	}

	InitializeDesiredState();
	RecomputeTargetInterval();
	InitSpoutDevices();
	ResetStats();

	if (bWantsReceiving && OutputRenderTarget)
	{
		StartReceiving();
	}
#endif
}

void USpoutReceiverComponent::ResetStats()
{
	CopiesPerSecond = 0.0f;
	FlushPerSecond = 0.0f;
	Flush1PerSecond = 0.0f;
	ReconnectCount = 0;
	MissedFrames = 0;

	StatCopyCount = 0;
	StatFlushCount = 0;
	StatFlush1Count = 0;
	StatMissedFramesAccum = 0;
	StatWindowStartSeconds = FPlatformTime::Seconds();
}

void USpoutReceiverComponent::UpdateStatsWindow()
{
	const double Now = FPlatformTime::Seconds();
	const double Elapsed = Now - StatWindowStartSeconds;

	if (Elapsed >= 1.0)
	{
		CopiesPerSecond = static_cast<float>(StatCopyCount / Elapsed);
		FlushPerSecond = static_cast<float>(StatFlushCount / Elapsed);
		Flush1PerSecond = static_cast<float>(StatFlush1Count / Elapsed);
		MissedFrames = static_cast<int32>(StatMissedFramesAccum);

		StatCopyCount = 0;
		StatFlushCount = 0;
		StatFlush1Count = 0;
		StatMissedFramesAccum = 0;
		StatWindowStartSeconds = Now;
	}
}

bool USpoutReceiverComponent::CacheDX11FenceObjects()
{
#if PLATFORM_WINDOWS
	if (CachedDev11_5 && CachedCtx11_4 && CopyFence11)
	{
		return true;
	}

	if (!SpoutDX12)
	{
		return false;
	}

	ID3D11Device* Dev11 = SpoutDX12->GetD3D11device();
	ID3D11DeviceContext* Ctx11 = SpoutDX12->GetD3D11context();

	if (!Dev11 || !Ctx11)
	{
		return false;
	}

	if (!CachedDev11_5)
	{
		HRESULT hr = Dev11->QueryInterface(
			__uuidof(ID3D11Device5),
			reinterpret_cast<void**>(&CachedDev11_5));

		if (FAILED(hr) || !CachedDev11_5)
		{
			UE_LOG(LogSpoutRX, Error, TEXT("QueryInterface(ID3D11Device5) failed. hr=0x%08X"), hr);
			return false;
		}
	}

	if (!CachedCtx11_4)
	{
		HRESULT hr = Ctx11->QueryInterface(
			__uuidof(ID3D11DeviceContext4),
			reinterpret_cast<void**>(&CachedCtx11_4));

		if (FAILED(hr) || !CachedCtx11_4)
		{
			UE_LOG(LogSpoutRX, Error, TEXT("QueryInterface(ID3D11DeviceContext4) failed. hr=0x%08X"), hr);
			return false;
		}
	}

	if (!CopyFence11)
	{
		HRESULT hr = CachedDev11_5->CreateFence(
			0,
			D3D11_FENCE_FLAG_NONE,
			__uuidof(ID3D11Fence),
			reinterpret_cast<void**>(&CopyFence11));

		if (FAILED(hr) || !CopyFence11)
		{
			UE_LOG(LogSpoutRX, Error, TEXT("CreateFence failed. hr=0x%08X"), hr);
			return false;
		}
	}

	return true;
#else
	return false;
#endif
}

void USpoutReceiverComponent::ReleaseFenceObjects()
{
#if PLATFORM_WINDOWS
	if (CopyFence11)
	{
		CopyFence11->Release();
		CopyFence11 = nullptr;
	}

	if (CachedCtx11_4)
	{
		CachedCtx11_4->Release();
		CachedCtx11_4 = nullptr;
	}

	if (CachedDev11_5)
	{
		CachedDev11_5->Release();
		CachedDev11_5 = nullptr;
	}

	NextFenceValue = 1;
	LastPublishedFenceValue = 0;

	for (int32 i = 0; i < 2; ++i)
	{
		SlotFenceState[i].FenceValue = 0;
		SlotFenceState[i].bPendingPublish = false;
	}
#endif
}

bool USpoutReceiverComponent::SignalSubmittedWork(int32 TrackedSlotIndex)
{
#if PLATFORM_WINDOWS
	if (!CacheDX11FenceObjects())
	{
		return false;
	}

	const uint64 FenceValue = NextFenceValue++;

	HRESULT hr = CachedCtx11_4->Signal(CopyFence11, FenceValue);
	if (FAILED(hr))
	{
		UE_LOG(LogSpoutRX, Error, TEXT("ID3D11DeviceContext4::Signal failed. hr=0x%08X"), hr);
		return false;
	}

	if (TrackedSlotIndex == 0 || TrackedSlotIndex == 1)
	{
		SlotFenceState[TrackedSlotIndex].FenceValue = FenceValue;
		SlotFenceState[TrackedSlotIndex].bPendingPublish = true;
	}

	return true;
#else
	return false;
#endif
}

void USpoutReceiverComponent::PublishCompletedInternalBuffer()
{
#if PLATFORM_WINDOWS
	if (!bUseDoubleBuffer || !OutputRenderTarget || !CopyFence11)
	{
		return;
	}

	const uint64 CompletedValue = CopyFence11->GetCompletedValue();

	int32 BestIndex = INDEX_NONE;
	uint64 BestFenceValue = LastPublishedFenceValue;

	for (int32 i = 0; i < 2; ++i)
	{
		if (SlotFenceState[i].bPendingPublish &&
			SlotFenceState[i].FenceValue > LastPublishedFenceValue &&
			SlotFenceState[i].FenceValue <= CompletedValue)
		{
			if (BestIndex == INDEX_NONE || SlotFenceState[i].FenceValue > BestFenceValue)
			{
				BestIndex = i;
				BestFenceValue = SlotFenceState[i].FenceValue;
			}
		}
	}

	if (BestIndex == INDEX_NONE)
	{
		return;
	}

	UTextureRenderTarget2D* ReadyRT = (BestIndex == 0) ? InternalRT_A : InternalRT_B;
	if (!ReadyRT)
	{
		return;
	}

	FTextureRenderTargetResource* SrcRes = ReadyRT->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* DstRes = OutputRenderTarget->GameThread_GetRenderTargetResource();

	if (!SrcRes || !DstRes)
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(SpoutCopyInternalToUserRT)(
		[SrcRes, DstRes](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* SrcTex = SrcRes->GetRenderTargetTexture();
			FRHITexture* DstTex = DstRes->GetRenderTargetTexture();

			if (SrcTex && DstTex)
			{
				FRHICopyTextureInfo Info;
				RHICmdList.CopyTexture(SrcTex, DstTex, Info);
			}
		});

	LastPublishedFenceValue = BestFenceValue;

	for (int32 i = 0; i < 2; ++i)
	{
		if (SlotFenceState[i].bPendingPublish &&
			SlotFenceState[i].FenceValue <= BestFenceValue)
		{
			SlotFenceState[i].bPendingPublish = false;
		}
	}

	// One-shot mode: stop only after the completed frame has been published.
	if (bPendingOneShotStop)
	{
		bPendingOneShotStop = false;
		StopReceiving();
	}
#endif
}

// Cleanup on end play.
void USpoutReceiverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopReceivingInternal(false);
	ReleaseSpoutDevices();
	Super::EndPlay(EndPlayReason);
}

// Tick update.
void USpoutReceiverComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bReceiving)
		return;

	// Poll fence completion every tick and publish completed internal buffer if ready.
	PublishCompletedInternalBuffer();

	if (TargetInterval <= 0.f)
	{
		// One-shot mode:
		// submit one frame, then stop only after PublishCompletedInternalBuffer() sees it complete.
		if (!bPendingOneShotStop)
		{
			if (ReceiveOnce())
			{
				bPendingOneShotStop = true;
			}
		}

		UpdateStatsWindow();
		return;
	}

	TickAccumulator += DeltaTime;
	if (TickAccumulator < TargetInterval)
		return;

	// Keep remainder to reduce jitter.
	TickAccumulator -= TargetInterval;

	ReceiveOnce();

	// Poll again in case the previous slot completed by now.
	PublishCompletedInternalBuffer();

	UpdateStatsWindow();
}

// Start receiving from Spout sender.
void USpoutReceiverComponent::StartReceiving()
{
	if (!IsSupportedWorld())
	{
		return;
	}

#if PLATFORM_WINDOWS
	bWantsReceiving = true;
	bReceiveIntentInitialized = true;

	if (!IsD3D12Active())
	{
		UE_LOG(LogSpoutRX, Warning,
			TEXT("SpoutReceiverComponent: D3D12 RHI is not active (current RHI: %s). Receiver is disabled."),
			GDynamicRHI ? *FString(GDynamicRHI->GetName()) : TEXT("None"));
		return;
	}
#endif

	if (!OutputRenderTarget)
	{
		UE_LOG(LogSpoutRX, Warning, TEXT("StartReceiving: OutputRenderTarget is null."));
		return;
	}

	if (!SpoutDX12 && !InitSpoutDevices())
	{
		UE_LOG(LogSpoutRX, Error, TEXT("Failed to init Spout devices."));
		return;
	}

	if (!SpoutDX12->OpenDirectX12(USpoutReceiverComponent::GetUE_D3D12Device(), nullptr))
	{
		UE_LOG(LogSpoutRX, Error, TEXT("SpoutDX12->OpenDirectX12 failed."));
		return;
	}

	if (!CacheDX11FenceObjects())
	{
		UE_LOG(LogSpoutRX, Error, TEXT("Failed to initialize D3D11 fence objects."));
		if (SpoutDX12)
		{
			SpoutDX12->ReleaseReceiver();
			SpoutDX12->CloseDirectX12();
		}
		ReleaseFenceObjects();
		return;
	}

	// Use selected sender name, or active sender if empty.
	if (!SpoutSenderName.IsEmpty())
	{
		SpoutDX12->SetReceiverName(TCHAR_TO_ANSI(*SpoutSenderName));
		UE_LOG(LogSpoutRX, Display, TEXT("Name is set!"));
	}
	else if (SpoutInfo)
	{
		char ActiveName[256] = {};
		if (SpoutInfo->GetActiveSender(ActiveName))
		{
			SpoutDX12->SetReceiverName(ActiveName);
		}
	}

	RecomputeTargetInterval();

	if (!bReceiving)
	{
		++ReconnectCount;
	}

	bPendingOneShotStop = false;
	LastPublishedFenceValue = 0;

	for (int32 i = 0; i < 2; ++i)
	{
		SlotFenceState[i].FenceValue = 0;
		SlotFenceState[i].bPendingPublish = false;
	}

	// Mark receiver as running.
	bReceiving = true;
	SetComponentTickEnabled(true);
	UE_LOG(LogSpoutRX, Display, TEXT("Spout receiver started @ %d FPS"), TargetFPS);
}

// Stop receiving from Spout sender.
void USpoutReceiverComponent::StopReceiving()
{
	StopReceivingInternal(true);
}

void USpoutReceiverComponent::StopReceivingInternal(bool bClearDesiredState)
{
	// Mark receiver as stopped.
	bReceiving = false;
	SetComponentTickEnabled(false);
	if (bClearDesiredState)
	{
		bWantsReceiving = false;
		bReceiveIntentInitialized = true;
	}

#if PLATFORM_WINDOWS
	FlushRenderingCommands();
#endif
	
	// Release resources and close Spout devices.
	for (int i = 0; i < 2; ++i)
	{
		if (Incoming.WrappedDest11[i])
		{
			reinterpret_cast<ID3D11Resource*>(Incoming.WrappedDest11[i])->Release();
			Incoming.WrappedDest11[i] = nullptr;
		}
		CachedRTRes[i] = nullptr;
		CachedRTObject[i] = nullptr;
	}

	InternalWriteIndex = 0;
	bPendingOneShotStop = false;

	if (Incoming.GPUCopy11)
	{
		reinterpret_cast<ID3D11Resource*>(Incoming.GPUCopy11)->Release();
		Incoming.GPUCopy11 = nullptr;
	}

	if (Incoming.CachedSrc11)
	{
		reinterpret_cast<ID3D11Texture2D*>(Incoming.CachedSrc11)->Release();
		Incoming.CachedSrc11 = nullptr;
	}
	Incoming.CachedShareHandle = nullptr;

	if (SpoutDX12)
	{
		SpoutDX12->ReleaseReceiver();
		SpoutDX12->CloseDirectX12();
	}

	ReleaseFenceObjects();

	bConnected = false;
}

bool USpoutReceiverComponent::EnsureUserOutputRT(uint32 W, uint32 H)
{
	if (!OutputRenderTarget) return false;

	// Match your existing behavior: only resize when size changes
	if ((uint32)OutputRenderTarget->SizeX == W && (uint32)OutputRenderTarget->SizeY == H)
		return true;

	const EPixelFormat NeededPF = MapDxgiToUE((DXGI_FORMAT)Incoming.Format);
	const bool bUseLinearGamma = !IsDXGISRGB((DXGI_FORMAT)Incoming.Format);

	OutputRenderTarget->InitCustomFormat(W, H, NeededPF, bUseLinearGamma);
	OutputRenderTarget->UpdateResourceImmediate(true);

	// Heavy but only on resize; keeps correctness
	FlushRenderingCommands();
	return true;
}

bool USpoutReceiverComponent::EnsureInternalRTs(uint32 W, uint32 H)
{
	if (!bUseDoubleBuffer)
		return true;

	if (!InternalRT_A)
		InternalRT_A = NewObject<UTextureRenderTarget2D>(this);

	if (!InternalRT_B)
		InternalRT_B = NewObject<UTextureRenderTarget2D>(this);

	// Initialize/resize to match incoming
	const EPixelFormat NeededPF = MapDxgiToUE((DXGI_FORMAT)Incoming.Format);
	const bool bUseLinearGamma = !IsDXGISRGB((DXGI_FORMAT)Incoming.Format);

	auto EnsureOne = [&](UTextureRenderTarget2D* RT)
		{
			if (!RT) return false;
			if ((uint32)RT->SizeX != W || (uint32)RT->SizeY != H)
			{
				RT->InitCustomFormat(W, H, NeededPF, bUseLinearGamma);
				RT->UpdateResourceImmediate(true);
				FlushRenderingCommands();
			}
			return true;
		};

	return EnsureOne(InternalRT_A) && EnsureOne(InternalRT_B);
}

// Return available Spout senders.
TArray<FString> USpoutReceiverComponent::GetAvailableSenders() const
{
	TArray<FString> Out;
	if (!SpoutInfo) return Out;

	FScopeLock _(&SpoutLock);

	const int count = SpoutInfo->GetSenderCount();
	Out.Reserve(count);
	char name[256];
	for (int i = 0; i < count; ++i)
	{
		name[0] = '\0';
		if (SpoutInfo->GetSender(i, name))
			Out.Add(UTF8_TO_TCHAR(name));
	}
	return Out;
}

// Initialize Spout objects.
bool USpoutReceiverComponent::InitSpoutDevices()
{
	if (!SpoutInfo)
		SpoutInfo = new spoutDX();
	if (!SpoutDX12)
		SpoutDX12 = new spoutDX12();
	return (SpoutInfo && SpoutDX12);
}

// Release resources and delete Spout objects.
void USpoutReceiverComponent::ReleaseSpoutDevices()
{
	for (int i = 0; i < 2; ++i)
	{
		if (Incoming.WrappedDest11[i])
		{
			reinterpret_cast<ID3D11Resource*>(Incoming.WrappedDest11[i])->Release();
			Incoming.WrappedDest11[i] = nullptr;
		}
		CachedRTRes[i] = nullptr;
		CachedRTObject[i] = nullptr;
	}

	InternalWriteIndex = 0;
	bPendingOneShotStop = false;

	if (Incoming.CachedSrc11)
	{
		reinterpret_cast<ID3D11Texture2D*>(Incoming.CachedSrc11)->Release();
		Incoming.CachedSrc11 = nullptr;
	}
	Incoming.CachedShareHandle = nullptr;

	if (Incoming.GPUCopy11)
	{
		reinterpret_cast<ID3D11Resource*>(Incoming.GPUCopy11)->Release();
		Incoming.GPUCopy11 = nullptr;
	}

	Incoming = FIncoming{};

	ReleaseFenceObjects();

	if (SpoutDX12)
	{
		SpoutDX12->ReleaseReceiver();
		SpoutDX12->CloseDirectX12();
		delete SpoutDX12;
		SpoutDX12 = nullptr;
	}
	if (SpoutInfo)
	{
		delete SpoutInfo;
		SpoutInfo = nullptr;
	}
}

// Receive one frame and copy it to UE render target.
bool USpoutReceiverComponent::ReceiveOnce()
{
	UE_LOG(LogSpoutRX, Verbose, TEXT("ReceiveOnce: ENTER"));

	if (!SpoutDX12)
	{
		UE_LOG(LogSpoutRX, Verbose, TEXT("Issue with Spout init!"));
		return false;
	}

	UE_LOG(LogSpoutRX, Verbose, TEXT("ReceiveOnce: begin"));

	// Read sender info from Spout.
	unsigned int SW = 0, SH = 0; HANDLE Share = nullptr; DWORD Fmt = 0;
	{
		const char* Name = SpoutDX12->GetSenderName();
		if (!Name || !SpoutInfo || !SpoutInfo->GetSenderInfo(Name, SW, SH, Share, Fmt) || !Share)
		{
			UE_LOG(LogSpoutRX, Error, TEXT("ReceiveOnce: no valid sender/share."));
			bConnected = false;
			++StatMissedFramesAccum;
			return false;
		}
		Incoming.Width = SW;
		Incoming.Height = SH;
		Incoming.Format = Fmt ? Fmt : DXGI_FORMAT_B8G8R8A8_UNORM;

		UE_LOG(LogSpoutRX, Verbose, TEXT("Sender '%S'  %ux%u  fmt=%u  Share=%p"),
			Name, SW, SH, Incoming.Format, Share);
	}

	// Open shared DX11 texture from sender.
	ID3D11Device* Dev11 = SpoutDX12 ? SpoutDX12->GetD3D11device() : nullptr;
	ID3D11DeviceContext* Ctx11 = SpoutDX12 ? SpoutDX12->GetD3D11context() : nullptr;
	if (!Dev11 || !Ctx11)
	{
		UE_LOG(LogSpoutRX, Warning, TEXT("ReceiveOnce: DX11 device/context unavailable."));
		++StatMissedFramesAccum;
		return false;
	}
	ID3D11Texture2D* Src11 = nullptr;

	// Cache by shared handle
	const void* ShareKey = (void*)Share;
	if (!Incoming.CachedSrc11 || Incoming.CachedShareHandle != ShareKey)
	{
		if (Incoming.CachedSrc11)
		{
			reinterpret_cast<ID3D11Texture2D*>(Incoming.CachedSrc11)->Release();
			Incoming.CachedSrc11 = nullptr;
		}

		Incoming.CachedShareHandle = (void*)Share;

		ID3D11Texture2D* Opened = nullptr;
		HRESULT hr = Dev11->OpenSharedResource(Share, __uuidof(ID3D11Texture2D),
			reinterpret_cast<void**>(&Opened));

		if (FAILED(hr) || !Opened)
		{
			UE_LOG(LogSpoutRX, Warning, TEXT("OpenSharedResource (DX11) failed. hr=0x%08X"), hr);
			Incoming.CachedShareHandle = nullptr;
			return false;
		}

		Incoming.CachedSrc11 = Opened; // keep the ref
	}

	Src11 = reinterpret_cast<ID3D11Texture2D*>(Incoming.CachedSrc11);

	// Create or resize intermediate GPU copy texture if needed.
	D3D11_TEXTURE2D_DESC sDesc{};
	Src11->GetDesc(&sDesc);
	UE_LOG(LogSpoutRX, Verbose, TEXT("Src11: %ux%u fmt=%u mips=%u"), sDesc.Width, sDesc.Height, sDesc.Format, sDesc.MipLevels);
	Incoming.Format = sDesc.Format;

	// Validate source texture description and format.
	if (sDesc.Width == 0 || sDesc.Height == 0 || sDesc.Format == DXGI_FORMAT_UNKNOWN) {
		UE_LOG(LogSpoutRX, Error, TEXT("Invalid Src11 desc: %ux%u fmt=%u"), sDesc.Width, sDesc.Height, sDesc.Format);
		return false;
	}
	const DXGI_FORMAT copyFmt = sDesc.Format;

	// Check if copy texture must be recreated.
	D3D11_TEXTURE2D_DESC curDesc{};
	if (Incoming.GPUCopy11) {
		reinterpret_cast<ID3D11Texture2D*>(Incoming.GPUCopy11)->GetDesc(&curDesc);
	}

	const bool needRecreate =
		(Incoming.GPUCopy11 == nullptr) ||
		(curDesc.Width != sDesc.Width) ||
		(curDesc.Height != sDesc.Height) ||
		(curDesc.Format != copyFmt);

	if (needRecreate)
	{
		if (Incoming.GPUCopy11) {
			reinterpret_cast<ID3D11Texture2D*>(Incoming.GPUCopy11)->Release();
			Incoming.GPUCopy11 = nullptr;
		}

		// Match source format and size. CopyResource needs exact match.
		D3D11_TEXTURE2D_DESC d{};
		d.Width = sDesc.Width;
		d.Height = sDesc.Height;
		d.MipLevels = 1;
		d.ArraySize = 1;
		d.Format = copyFmt;
		d.SampleDesc.Count = 1;
		d.SampleDesc.Quality = 0;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = 0;
		d.CPUAccessFlags = 0;
		d.MiscFlags = 0;

		// Create copy texture.
		ComPtr<ID3D11Texture2D> copyTex;
		HRESULT hr = Dev11->CreateTexture2D(&d, nullptr, copyTex.GetAddressOf());
		if (FAILED(hr) || !copyTex) {
			UE_LOG(LogSpoutRX, Error, TEXT("CreateTexture2D failed. hr=0x%08X  %ux%u fmt=%u"),
				hr, d.Width, d.Height, d.Format);
			return false;
		}

		// Store created texture.
		Incoming.GPUCopy11 = copyTex.Detach();
		UE_LOG(LogSpoutRX, Verbose, TEXT("GPUCopy11 created %ux%u fmt=%u"), d.Width, d.Height, d.Format);
	}


	// Copy sender texture to local DX11 copy texture.
	Ctx11->CopyResource(
		reinterpret_cast<ID3D11Resource*>(Incoming.GPUCopy11),
		reinterpret_cast<ID3D11Resource*>(Src11)
	);

	++StatCopyCount;

	// Always keep user output RT valid (stable object)
	if (!OutputRenderTarget || !EnsureUserOutputRT(Incoming.Width, Incoming.Height))
	{
		UE_LOG(LogSpoutRX, Warning, TEXT("User OutputRenderTarget missing or failed to init."));
		return false;
	}

	// Choose internal write RT when enabled; otherwise keep old behavior (write directly to user RT)
	UTextureRenderTarget2D* WriteRT = OutputRenderTarget;
	int32 WriteIdx = 0;

	if (bUseDoubleBuffer)
	{
		if (!EnsureInternalRTs(Incoming.Width, Incoming.Height))
		{
			UE_LOG(LogSpoutRX, Error, TEXT("Failed to init internal RTs."));
			return false;
		}

		WriteIdx = (InternalWriteIndex == 0) ? 0 : 1;
		WriteRT = (WriteIdx == 0) ? InternalRT_A : InternalRT_B;
	}

	// Ensure destination is wrapped (wraps internal RT or user RT depending on mode)
	const bool bDestOK = EnsureGpuRenderTarget(Incoming.Width, Incoming.Height, WriteIdx, WriteRT);
	if (!bDestOK) { UE_LOG(LogSpoutRX, Error, TEXT("ReceiveOnce: ensure dest failed")); return false; }
	if (!Incoming.WrappedDest11[WriteIdx]) { UE_LOG(LogSpoutRX, Error, TEXT("ReceiveOnce: WrappedDest11 null")); return false; }

	// Copy local DX11 texture to wrapped UE render target.
	ID3D11On12Device* D3D11On12 = USpoutReceiverComponent::GetD3D11On12(SpoutDX12);
	if (!D3D11On12)
	{
		UE_LOG(LogSpoutRX, Error, TEXT("D3D11On12 device unavailable."));
		return false;
	}

	ID3D11Resource* ToAcquire[1] = { reinterpret_cast<ID3D11Resource*>(Incoming.WrappedDest11[WriteIdx]) };
	D3D11On12->AcquireWrappedResources(ToAcquire, 1);

	Ctx11->CopyResource(
		reinterpret_cast<ID3D11Resource*>(Incoming.WrappedDest11[WriteIdx]),
		reinterpret_cast<ID3D11Resource*>(Incoming.GPUCopy11)
	);

	D3D11On12->ReleaseWrappedResources(ToAcquire, 1);

	// Fence-based submit:
	// signal after ReleaseWrappedResources so the fence represents completed D3D11On12 submission order.
	const int32 TrackedSlotIndex = bUseDoubleBuffer ? WriteIdx : INDEX_NONE;

	if (!SignalSubmittedWork(TrackedSlotIndex))
	{
		UE_LOG(LogSpoutRX, Error, TEXT("Failed to signal D3D11 fence after copy."));
		++StatMissedFramesAccum;
		return false;
	}

	// No per-frame Flush / Flush1 here.
	// Double-buffer publish is handled later by PublishCompletedInternalBuffer()
	// only after the fence completes.

	bConnected = true;

	if (bUseDoubleBuffer)
	{
		InternalWriteIndex = 1 - WriteIdx;
	}

	UE_LOG(LogSpoutRX, Verbose, TEXT("ReceiveOnce!"));
	return true;
}

// Ensure UE render target exists and is wrapped for DX11.
bool USpoutReceiverComponent::EnsureGpuRenderTarget(uint32 W, uint32 H, int32 Index, UTextureRenderTarget2D* TargetRT)
{
	if (!TargetRT) return false;
	Index = (Index == 0) ? 0 : 1;

	// Invalidate cache if TargetRT object changed for this slot
	if (CachedRTObject[Index] != TargetRT)
	{
		CachedRTObject[Index] = TargetRT;
		CachedRTRes[Index] = nullptr;

		if (Incoming.WrappedDest11[Index])
		{
			reinterpret_cast<ID3D11Resource*>(Incoming.WrappedDest11[Index])->Release();
			Incoming.WrappedDest11[Index] = nullptr;
		}
	}

	// Resize TargetRT if needed (size only)
	bool bReinit = ((uint32)TargetRT->SizeX != W) || ((uint32)TargetRT->SizeY != H);
	if (bReinit)
	{
		const EPixelFormat NeededPF = MapDxgiToUE((DXGI_FORMAT)Incoming.Format);
		const bool bUseLinearGamma = !IsDXGISRGB((DXGI_FORMAT)Incoming.Format);

		TargetRT->InitCustomFormat(W, H, NeededPF, bUseLinearGamma);
		TargetRT->UpdateResourceImmediate(true);
		FlushRenderingCommands();

		if (Incoming.WrappedDest11[Index])
		{
			reinterpret_cast<ID3D11Resource*>(Incoming.WrappedDest11[Index])->Release();
			Incoming.WrappedDest11[Index] = nullptr;
		}
		CachedRTRes[Index] = nullptr;
	}

	FTextureRenderTargetResource* RTRes = TargetRT->GameThread_GetRenderTargetResource();
	if (!RTRes)
	{
		UE_LOG(LogSpoutRX, Display, TEXT("EnsureGpuRT: no RT resource (game thread)"));
		return false;
	}

	// Already wrapped for this exact render resource
	if (Incoming.WrappedDest11[Index] && CachedRTRes[Index] == RTRes)
		return true;

	// Re-wrap
	if (Incoming.WrappedDest11[Index])
	{
		reinterpret_cast<ID3D11Resource*>(Incoming.WrappedDest11[Index])->Release();
		Incoming.WrappedDest11[Index] = nullptr;
	}

	FRenderCommandFence Fence;
	bool bWrapOk = false;

	ENQUEUE_RENDER_COMMAND(WrapSpoutRT)(
		[this, RTRes, Index, &bWrapOk](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* RHI = RTRes->GetRenderTargetTexture();
			if (!RHI) { bWrapOk = false; return; }

			ID3D12Resource* DestDX12 = (ID3D12Resource*)RHI->GetNativeResource();
			if (!DestDX12) { bWrapOk = false; return; }

			ID3D11Resource* Wrapped = nullptr;
			if (!SpoutDX12->WrapDX12Resource(DestDX12, &Wrapped, D3D12_RESOURCE_STATE_COPY_DEST))
			{
				bWrapOk = false;
				return;
			}

			Incoming.WrappedDest11[Index] = Wrapped;
			bWrapOk = true;
		});

	Fence.BeginFence();
	Fence.Wait(false);

	if (bWrapOk)
		CachedRTRes[Index] = RTRes;

	return bWrapOk;
}
// Get UE D3D12 device.
ID3D12Device* USpoutReceiverComponent::GetUE_D3D12Device()
{
	void* Native = GDynamicRHI ? GDynamicRHI->RHIGetNativeDevice() : nullptr;
	return reinterpret_cast<ID3D12Device*>(Native);
}
// Get D3D11On12 device from SpoutDX12.
ID3D11On12Device* USpoutReceiverComponent::GetD3D11On12(spoutDX12* InDX12)
{
	return InDX12 ? InDX12->GetD3D11On12device() : nullptr;
}
