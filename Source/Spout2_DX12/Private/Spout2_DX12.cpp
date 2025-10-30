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
    TArray<FString> Dirs;
    if (const TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(TEXT("Spout2_DX12")))
        Dirs.Add(FPaths::Combine(P->GetBaseDir(), TEXT("Binaries/Win64")));                // plugin binaries (Editor)
    Dirs.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Win64")));               // project binaries (Editor)
    Dirs.Add(FPlatformProcess::BaseDir());

    // Load the DLL
    for (const FString& Dir : Dirs) {
        FPlatformProcess::PushDllDirectory(*Dir);
		MyDllHandle = MyDllHandle ? MyDllHandle : FPlatformProcess::GetDllHandle(TEXT("SpoutDX12.dll"));
		if (MyDllHandle) break;
		FPlatformProcess::PopDllDirectory(*Dir);
    }
    if (MyDllHandle == nullptr)
    {
        // Handle error: DLL failed to load
        UE_LOG(LogTemp, Error, TEXT("Failed to load SpoutDX12.dll"));
    }
    else
    {
        // Success: DLL is loaded and ready for use
        UE_LOG(LogTemp, Log, TEXT("Successfully loaded SpoutDX12.dll"));
    }
#endif
}

// Spout2_DX12 module shutdown
void FSpout2_DX12Module::ShutdownModule()
{
#if PLATFORM_WINDOWS
	// Free the DLL handle
    if (MyDllHandle) { FPlatformProcess::FreeDllHandle(MyDllHandle); MyDllHandle = nullptr; }
#endif
}

#undef LOCTEXT_NAMESPACE
IMPLEMENT_MODULE(FSpout2_DX12Module, Spout2_DX12)
