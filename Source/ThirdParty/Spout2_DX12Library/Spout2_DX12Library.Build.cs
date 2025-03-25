// Fill out your copyright notice in the Description page of Project Settings.

using System.IO;
using UnrealBuildTool;

public class Spout2_DX12Library : ModuleRules
{
	public Spout2_DX12Library(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string LibPath = Path.Combine(ModuleDirectory, "x64", "Release");
            string DllPath = "$(PluginDir)/Binaries/ThirdParty/Spout2_DX12Library/Win64";

            PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "include", "SpoutDX12"));

            // Add libs
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "SpoutDX12.lib"));

            // Delay-load DLL
            PublicDelayLoadDLLs.Add("SpoutDX12.dll");

            // Stage the DLL with the build
            RuntimeDependencies.Add(Path.Combine(DllPath, "SpoutDX12.dll"));
        }
    }
}
