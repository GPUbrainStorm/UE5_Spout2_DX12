#include "Spout2_DX12.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RHI.h"
#include "Misc/MessageDialog.h"
#include "Spout2BlueprintLibrary.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "RenderingThread.h"
#include "SpoutDX12.h"
#include "Modules/ModuleManager.h"
#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "TextureResource.h"

#define LOCTEXT_NAMESPACE "FSpout2_DX12Module"

// Spout2_DX12 module implementation
void FSpout2_DX12Module::StartupModule()
{
#if PLATFORM_WINDOWS
	// Load SpoutDX12.dll
	FString BaseDir = IPluginManager::Get().FindPlugin("Spout2_DX12")->GetBaseDir();
	FString DllPath = FPaths::Combine(*BaseDir, TEXT("Binaries/ThirdParty/Spout2_DX12Library/Win64/SpoutDX12.dll"));
	SpoutLibraryHandle = FPlatformProcess::GetDllHandle(*DllPath);

	if (!SpoutLibraryHandle)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("SpoutLoadFail", "Failed to load SpoutDX12.dll"));
		return;
	}

	// Initialize Spout's DX12 wrapper (creates internal DX12 + DX11on12 devices)
	SpoutBridge.OpenDirectX12();
#endif
}

// Spout2_DX12 module shutdown
void FSpout2_DX12Module::ShutdownModule()
{
#if PLATFORM_WINDOWS
	// Close Spout's DX12 wrapper
	SpoutBridge.CloseDirectX12();
	// Free SpoutDX12.dll
	if (SpoutLibraryHandle)
	{
		FPlatformProcess::FreeDllHandle(SpoutLibraryHandle);
		SpoutLibraryHandle = nullptr;
	}
#endif
}

// Send a UTextureRenderTarget2D to Spout
void FSpout2_DX12Module::SendRenderTarget(UTextureRenderTarget2D* RenderTarget, const FString& InSenderName)
{
#if PLATFORM_WINDOWS
	if (!RenderTarget || InSenderName.IsEmpty()) return;

	// Get the render target's resource
	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	// Get the render target's texture
	FTextureRHIRef RHITexture = RTResource->GetRenderTargetTexture();
	if (!RHITexture.IsValid()) return;

	// Copy SpoutBridge by reference
	spoutDX12* LocalSpoutBridge = &SpoutBridge;

	// Send the render target to Spout using ENQUEUE_RENDER_COMMAND for thread safety on the render thread
	ENQUEUE_RENDER_COMMAND(SendSpoutRenderTarget)(
		[RHITexture, LocalSpoutBridge, InSenderName](FRHICommandListImmediate& RHICmdList)
		{
		// Get the native DX12 resource
		ID3D12Resource* NativeTexture = static_cast<ID3D12Resource*>(RHITexture->GetNativeResource());
		if (!NativeTexture || !LocalSpoutBridge) return;

		// Set the sender name
		LocalSpoutBridge->SetSenderName(TCHAR_TO_ANSI(*InSenderName));

		// Wrap the DX12 resource in a DX11 resource and send it to Spout
		ID3D11Resource* Wrapped11Resource = nullptr;
		if (LocalSpoutBridge->WrapDX12Resource(NativeTexture, &Wrapped11Resource, D3D12_RESOURCE_STATE_RENDER_TARGET) && Wrapped11Resource)
		{
			LocalSpoutBridge->SendDX11Resource(Wrapped11Resource);
			Wrapped11Resource->Release();
		}
		}
		);
#endif
}

// Update a UTextureRenderTarget2D in Spout
void FSpout2_DX12Module::UpdateTexture(UTextureRenderTarget2D* RenderTarget, const FString& InSenderName)
{
#if PLATFORM_WINDOWS
	if (!RenderTarget || InSenderName.IsEmpty()) return;

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource) return;

	FTextureRHIRef RHITexture = RTResource->GetRenderTargetTexture();
	if (!RHITexture.IsValid()) return;

	spoutDX12* LocalSpoutBridge = &SpoutBridge;

	ENQUEUE_RENDER_COMMAND(UpdateSpoutRenderTarget)(
		[RHITexture, LocalSpoutBridge, InSenderName](FRHICommandListImmediate& RHICmdList)
		{
			ID3D12Resource* NativeTexture = static_cast<ID3D12Resource*>(RHITexture->GetNativeResource());
			if (!NativeTexture || !LocalSpoutBridge) return;

			ID3D11Resource* Wrapped11Resource = nullptr;

			if (LocalSpoutBridge->WrapDX12Resource(NativeTexture, &Wrapped11Resource, D3D12_RESOURCE_STATE_RENDER_TARGET) && Wrapped11Resource)
			{
				LocalSpoutBridge->SendDX11Resource(Wrapped11Resource);
				Wrapped11Resource->Release();
			}
		});
#endif
}

#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FSpout2_DX12Module, Spout2_DX12)
