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

	TickAccumulator += DeltaTime;
	if (TickAccumulator < TargetInterval)
		return;

	TickAccumulator = 0.f;
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
	if (Incoming.WrappedDest11)
	{
		reinterpret_cast<ID3D11Resource*>(Incoming.WrappedDest11)->Release();
		Incoming.WrappedDest11 = nullptr;
	}

	if (Incoming.GPUCopy11)
	{
		reinterpret_cast<ID3D11Resource*>(Incoming.GPUCopy11)->Release();
		Incoming.GPUCopy11 = nullptr;
	}

	if (SpoutDX12)
	{
		SpoutDX12->ReleaseReceiver();
		SpoutDX12->CloseDirectX12();
	}

	bConnected = false;
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
	if (Incoming.WrappedDest11)
	{
		reinterpret_cast<ID3D11Resource*>(Incoming.WrappedDest11)->Release();
	}
	if (Incoming.GPUCopy11)
	{
		reinterpret_cast<ID3D11Resource*>(Incoming.GPUCopy11)->Release();
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
	ComPtr<ID3D11Texture2D> Src11;
	{
		HRESULT hr = Dev11->OpenSharedResource(Share, __uuidof(ID3D11Texture2D),
			reinterpret_cast<void**>(Src11.GetAddressOf()));
		if (FAILED(hr) || !Src11)
		{
			UE_LOG(LogSpoutRX, Warning, TEXT("OpenSharedResource (DX11) failed. hr=0x%08X"), hr);
			return false;
		}

		D3D11_TEXTURE2D_DESC sDesc{}; Src11->GetDesc(&sDesc);
		UE_LOG(LogSpoutRX, Verbose, TEXT("Src11: %ux%u fmt=%u mips=%u"), sDesc.Width, sDesc.Height, sDesc.Format, sDesc.MipLevels);
	}

	// Create or resize intermediate GPU copy texture if needed.
	D3D11_TEXTURE2D_DESC sDesc{};
	Src11->GetDesc(&sDesc);
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
		reinterpret_cast<ID3D11Resource*>(Incoming.GPUCopy11), // Dst
		Src11.Get()                                            // Src
	);

	// Ensure UE render target exists and is wrapped for DX11.
	bool bDestOK = false;
	if (OutputRenderTarget) {
		bDestOK = EnsureGpuRenderTarget(Incoming.Width, Incoming.Height);
	} else 
	{
		UE_LOG(LogSpoutRX, Warning, TEXT("No selected Render Target!"));
		return false;
	}
		
	// Check wrapped destination resource.
	if (!bDestOK) { UE_LOG(LogSpoutRX, Error, TEXT("ReceiveOnce: ensure dest failed")); return false; }
	if (!Incoming.WrappedDest11) { UE_LOG(LogSpoutRX, Error, TEXT("ReceiveOnce: WrappedDest11 null")); return false; }

	// Copy local DX11 texture to wrapped UE render target.
	ID3D11On12Device* D3D11On12 = USpoutReceiverComponent::GetD3D11On12(SpoutDX12);
	if (!D3D11On12)
	{
		UE_LOG(LogSpoutRX, Error, TEXT("D3D11On12 device unavailable."));
		return false;
	}

	// Acquire wrapped resource, copy, then release it.
	ID3D11Resource* ToAcquire[1] = { reinterpret_cast<ID3D11Resource*>(Incoming.WrappedDest11) };
	D3D11On12->AcquireWrappedResources(ToAcquire, 1);

	Ctx11->CopyResource(
		reinterpret_cast<ID3D11Resource*>(Incoming.WrappedDest11),
		reinterpret_cast<ID3D11Resource*>(Incoming.GPUCopy11)
	);

	D3D11On12->ReleaseWrappedResources(ToAcquire, 1);
	// Flush to finish copy now.
	Ctx11->Flush();
	UE_LOG(LogSpoutRX, Verbose, TEXT("Copy submitted + Flush"));
	// Mark as connected.
	bConnected = true;
	UE_LOG(LogSpoutRX, Verbose, TEXT("ReceiveOnce!"));

	// If TargetFPS <= 0, receive one frame then stop.
	if (TargetFPS <= 0) {
		StopReceiving();
	}
	return true;
}

// Ensure UE render target exists and is wrapped for DX11.
bool USpoutReceiverComponent::EnsureGpuRenderTarget(uint32 W, uint32 H)
{
	if (!OutputRenderTarget) return false;
	// Pick needed pixel format and gamma.
	const EPixelFormat NeededPF = MapDxgiToUE(static_cast<DXGI_FORMAT>(Incoming.Format));
	const bool bUseLinearGamma = !IsDXGISRGB(static_cast<DXGI_FORMAT>(Incoming.Format));
	// Check if render target needs reinit.
	bool bReinit = false;
	// Reinit only when size changes.
	if ((uint32)OutputRenderTarget->SizeX != W ||
		(uint32)OutputRenderTarget->SizeY != H) {
		bReinit = true;
	}
	// Reinit texture when needed.
	if (bReinit) {
		OutputRenderTarget->InitCustomFormat(W, H, NeededPF, bUseLinearGamma);
		OutputRenderTarget->UpdateResourceImmediate(true);
		FlushRenderingCommands();
	}

	// Release old D3D11 wrapper if it exists.
	if (Incoming.WrappedDest11) {
		reinterpret_cast<ID3D11Resource*>(Incoming.WrappedDest11)->Release();
		Incoming.WrappedDest11 = nullptr;
	}
	
	// Get render target resource on game thread.
	FTextureRenderTargetResource* RTRes = OutputRenderTarget->GameThread_GetRenderTargetResource();
	if (!RTRes) {
		UE_LOG(LogSpoutRX, Display, TEXT("EnsureGpuRT: no RT resource (game thread)"));
		return false;
	}

	// Queue render command to wrap UE render target for DX11.
	FRenderCommandFence Fence;
	bool bWrapOk = false;

	// Use RTRes on render thread.
	ENQUEUE_RENDER_COMMAND(WrapSpoutRT)(
		[this, RTRes, &bWrapOk](FRHICommandListImmediate& RHICmdList)
	{
		// Get RHI texture.
		FRHITexture* RHI = RTRes->GetRenderTargetTexture();
		if (!RHI) {
			UE_LOG(LogSpoutRX, Display, TEXT("EnsureGpuRT: no RHI texture (render thread)"));
			bWrapOk = false; return;
		}
		// Get native D3D12 resource.
		ID3D12Resource* DestDX12 = (ID3D12Resource*)RHI->GetNativeResource();
		if (!DestDX12) {
			UE_LOG(LogSpoutRX, Display, TEXT("EnsureGpuRT: no native D3D12 (render thread)"));
			bWrapOk = false; return;
		}
		// Wrap D3D12 resource for DX11 use.
		ID3D11Resource* Wrapped = nullptr;
		if (!SpoutDX12->WrapDX12Resource(DestDX12, &Wrapped, D3D12_RESOURCE_STATE_COPY_DEST)) {
			UE_LOG(LogSpoutRX, Display, TEXT("EnsureGpuRT: WrapDX12Resource failed"));
			bWrapOk = false; return;
		}
		// Store wrapped resource.
		Incoming.WrappedDest11 = Wrapped;
		bWrapOk = true;
	});
	// Wait for render command to finish.
	Fence.BeginFence();
	Fence.Wait(false);

	UE_LOG(LogSpoutRX, Verbose, TEXT("EnsureGpuRenderTarget: %ux%u resized=%d wrap=%d"),
		W, H, bReinit ? 1 : 0, bWrapOk ? 1 : 0);
	// Return wrap result.
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