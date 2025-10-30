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
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

#define LOCTEXT_NAMESPACE "FSpout2_DX12Module"
DEFINE_LOG_CATEGORY(LogSpoutRX);

// Spout2_DX12 module implementation
void FSpout2_DX12Module::StartupModule()
{
#if PLATFORM_WINDOWS
	// Initialize Spout's DX12 bridge 
	SpoutBridge.OpenDirectX12();
#endif
}

// Spout2_DX12 module shutdown
void FSpout2_DX12Module::ShutdownModule()
{
#if PLATFORM_WINDOWS
	// Close Spout's DX12 bridge
	SpoutBridge.CloseDirectX12();
#endif
}

#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FSpout2_DX12Module, Spout2_DX12)
