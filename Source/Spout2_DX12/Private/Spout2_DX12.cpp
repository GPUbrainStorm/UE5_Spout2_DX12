#include "Spout2_DX12.h"
#include "Interfaces/IPluginManager.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RHI.h"
#include "Misc/MessageDialog.h"
#include "Spout2BlueprintLibrary.h"
#include "Engine/Engine.h"
#include "EngineGlobals.h"
#include "RenderingThread.h"
#include "Modules/ModuleManager.h"
#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "TextureResource.h"

#if PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Windows/WindowsHWrapper.h"

THIRD_PARTY_INCLUDES_START
#include "Windows/AllowWindowsPlatformTypes.h"

#include <d3d11.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi.h>

// include Spout only *here*, never above
#include "SpoutDX12.h"

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

    // Try known folders one by one.
    // Always pop after push so we do not leave extra DLL dirs on the process stack.
    for (const FString& Dir : Dirs) {
        FPlatformProcess::PushDllDirectory(*Dir);
        void* TryHandle = FPlatformProcess::GetDllHandle(TEXT("SpoutDX12.dll"));
        FPlatformProcess::PopDllDirectory(*Dir);

        if (TryHandle)
        {
            MyDllHandle = TryHandle;
            break;
        }
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
