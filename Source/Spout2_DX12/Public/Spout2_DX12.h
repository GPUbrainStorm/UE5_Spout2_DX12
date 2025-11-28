#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

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