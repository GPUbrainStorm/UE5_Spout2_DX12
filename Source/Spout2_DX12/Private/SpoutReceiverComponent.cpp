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

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"

THIRD_PARTY_INCLUDES_START
#include "Windows/AllowWindowsPlatformTypes.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

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

#include <d3d11_3.h>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
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

// Constructor / destructor.
USpoutReceiverComponent::USpoutReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

USpoutReceiverComponent::~USpoutReceiverComponent()
{
	StopReceiving();
	ReleaseSpoutDevices();
}

// Set interval, init Spout, and auto-start if enabled.
void USpoutReceiverComponent::BeginPlay()
{
	Super::BeginPlay();

	if (TargetFPS>0) {
		TargetFPS = FMath::Clamp(TargetFPS, 1, 240);
		TargetInterval = 1.0f / float(TargetFPS);
	}
	else {
		TargetInterval = 0.f;
	}

	InitSpoutDevices();

	if (bAutoStart)
	{
		StartReceiving();
	}
}

// Cleanup on end play.
void USpoutReceiverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopReceiving();
	ReleaseSpoutDevices();
	Super::EndPlay(EndPlayReason);
}

// Tick update.
void USpoutReceiverComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bReceiving)
		return;

	if (TargetInterval <= 0.f)
	{
		// "Receive once then stop" mode relies on ReceiveOnce() calling StopReceiving()
		ReceiveOnce();
		return;
	}

	TickAccumulator += DeltaTime;
	if (TickAccumulator < TargetInterval)
		return;

	// keep remainder to reduce jitter
	TickAccumulator -= TargetInterval;

	ReceiveOnce();

}

// Start receiving from Spout sender.
void USpoutReceiverComponent::StartReceiving()
{
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

	// Recompute interval from TargetFPS.
	if (TargetFPS > 0) {
		TargetFPS = FMath::Clamp(TargetFPS, 1, 240);
		TargetInterval = 1.0f / float(TargetFPS);
	}
	else {
		TargetInterval = 0.f;
	}

	// Mark receiver as running.
	bReceiving = true;
	UE_LOG(LogSpoutRX, Display, TEXT("Spout receiver started @ %d FPS"), TargetFPS);
}

// Stop receiving from Spout sender.
void USpoutReceiverComponent::StopReceiving()
{
	// Mark receiver as stopped.
	bReceiving = false;
	
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
	InternalReadyIndex = -1;
	bSeededOutput = false;

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
	InternalReadyIndex = -1;
	bSeededOutput = false;

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

	// No per-frame flush in stable-user-RT internal double buffer mode.
	// Keep flush in legacy single-buffer mode (current working behavior).
	// Submit D3D11on12 work every frame.
// Without a submit/flush, updates can appear ~1 FPS because commands stay buffered. :contentReference[oaicite:1]{index=1}
	if (ID3D11DeviceContext3* Ctx3 = nullptr;
		SUCCEEDED(Ctx11->QueryInterface(__uuidof(ID3D11DeviceContext3), (void**)&Ctx3)) && Ctx3)
	{
		// Flush1 is asynchronous and can be less disruptive than Flush in some drivers. :contentReference[oaicite:2]{index=2}
		Ctx3->Flush1(D3D11_CONTEXT_TYPE_ALL, nullptr);
		Ctx3->Release();
	}
	else
	{
		Ctx11->Flush();
	}
	UE_LOG(LogSpoutRX, Verbose, TEXT("Copy submitted + Flush"));
	// Mark as connected.
	bConnected = true;
	if (bUseDoubleBuffer)
	{
		// Publish the previously completed internal buffer into the user RT (stable object).
		// First frame: seed output once using a flush to avoid initial blank.
		if (InternalReadyIndex < 0)
		{
			// Seed: force completion once, then publish the just-written buffer.
			Ctx11->Flush();
			InternalReadyIndex = WriteIdx;
			bSeededOutput = true;
		}
		else
		{
			// Normal: display previous completed buffer (avoids black frames without per-frame flush)
			UTextureRenderTarget2D* ReadyRT = (InternalReadyIndex == 0) ? InternalRT_A : InternalRT_B;

			FTextureRenderTargetResource* SrcRes = ReadyRT->GameThread_GetRenderTargetResource();
			FTextureRenderTargetResource* DstRes = OutputRenderTarget->GameThread_GetRenderTargetResource();

			if (SrcRes && DstRes)
			{
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
			}

			InternalReadyIndex = WriteIdx;
		}

		InternalWriteIndex = 1 - WriteIdx;
	}
	UE_LOG(LogSpoutRX, Verbose, TEXT("ReceiveOnce!"));

	// If TargetFPS <= 0, receive one frame then stop.
	if (TargetFPS <= 0) {
		StopReceiving();
	}
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