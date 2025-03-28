#pragma once

#include "Modules/ModuleManager.h"
#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "SpoutDX12.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"


class SPOUT2_DX12_API FSpout2_DX12Module : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void SendRenderTarget(UTextureRenderTarget2D* RenderTarget, const FString& InSenderName);
	void UpdateTexture();

	static inline FSpout2_DX12Module& Get()
	{
		return FModuleManager::LoadModuleChecked<FSpout2_DX12Module>("Spout2_DX12");
	}

private:
	spoutDX12 SpoutBridge;
	void* SpoutLibraryHandle = nullptr;
	ID3D11Resource* CurrWrappedResource = nullptr;

};