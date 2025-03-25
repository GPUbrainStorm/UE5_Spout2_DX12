// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Spout2_DX12 : ModuleRules
{
	public Spout2_DX12(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
				//Path.Combine(ModuleDirectory, "../Spout2_DX12Library/include/SpoutDX12")
            }
			);

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
        PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
            {
                "Core",
                "Spout2_DX12Library",
                "Projects",
                "CoreUObject",
                "Engine",
                "RenderCore",
                "RHI",
                "Slate",
                "SlateCore"
				// ... add other public dependencies that you statically link with here ...
			}
            );
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
            {
				// ... add private dependencies that you statically link with here ...
				"Engine",
                "RenderCore",
                "RHI"
            }
            );
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

        string DllPath = "$(PluginDir)/Binaries/ThirdParty/Spout2_DX12Library/Win64/SpoutDX12.dll";
        PublicDelayLoadDLLs.Add(DllPath);
        RuntimeDependencies.Add(DllPath);
    }
}
