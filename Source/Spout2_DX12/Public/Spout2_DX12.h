#pragma once

#include "Modules/ModuleManager.h"
#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "SpoutDX12.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpoutRX, Log, All);

class SPOUT2_DX12_API FSpout2_DX12Module : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static inline FSpout2_DX12Module& Get()
	{
		return FModuleManager::LoadModuleChecked<FSpout2_DX12Module>("Spout2_DX12");
	}

private:
	void* MyDllHandle = nullptr;
};