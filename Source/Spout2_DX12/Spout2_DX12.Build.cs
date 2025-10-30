// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Spout2_DX12 : ModuleRules
{
	public Spout2_DX12(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "..", "ThirdParty", "include"),
            Path.Combine(ModuleDirectory, "Public"),
        });

        PrivateIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "Private"),
        });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "Projects",
            "CoreUObject",
            "Engine",
            "RenderCore",
            "RHI",
            "D3D12RHI",
            "Slate",
            "SlateCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Engine",
            "RenderCore",
            "RHI",
            "D3D12RHI",
        });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string TP = Path.Combine(ModuleDirectory, "..", "ThirdParty");
            string LibDir = Path.Combine(TP, "lib", "Win64");
            string BinDir = Path.Combine(TP, "bin", "Win64");

            PublicIncludePaths.Add(Path.Combine(TP, "include"));
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, "Spout.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, "SpoutDX12.lib"));
            PublicDelayLoadDLLs.AddRange(new[] { "Spout.dll", "SpoutDX12.dll" });

            // Runtime dependencies - Editor
            if (Target.Type == TargetType.Editor)
            {
                // Copy the dll next to the project's Editor binaries
                RuntimeDependencies.Add("$(ProjectDir)/Binaries/Win64/Spout.dll", Path.Combine(BinDir, "Spout.dll"));
                RuntimeDependencies.Add("$(ProjectDir)/Binaries/Win64/SpoutDX12.dll", Path.Combine(BinDir, "SpoutDX12.dll"));
            }
            else // Game/Client/Server
            {
                // Normal build outputs
                RuntimeDependencies.Add("$(BinaryOutputDir)/Spout.dll", Path.Combine(BinDir, "Spout.dll"));
                RuntimeDependencies.Add("$(BinaryOutputDir)/SpoutDX12.dll", Path.Combine(BinDir, "SpoutDX12.dll"));
            }

            RuntimeDependencies.Add(Path.Combine(BinDir, "Spout.dll"), StagedFileType.NonUFS);
            RuntimeDependencies.Add(Path.Combine(BinDir, "SpoutDX12.dll"), StagedFileType.NonUFS);
        }
    }
}