// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class Spout2_DX12 : ModuleRules
{
    public Spout2_DX12(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Includes
        PublicIncludePaths.AddRange(new[]
        {
            Path.Combine(PluginDirectory, "Source", "ThirdParty", "include"),
            Path.Combine(ModuleDirectory, "Public"),
        });

        PrivateIncludePaths.AddRange(new[]
        {
            Path.Combine(ModuleDirectory, "Private"),
        });

        // UE deps
        PublicDependencyModuleNames.AddRange(new[]
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

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "Engine",
            "RenderCore",
            "RHI",
            "D3D12RHI",
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string TP = Path.Combine(PluginDirectory, "Source", "ThirdParty");
            string IncDir = Path.Combine(TP, "include");
            string LibDir = Path.Combine(TP, "lib", "Win64");
            string BinDir = Path.Combine(TP, "bin", "Win64");

            PublicIncludePaths.Add(IncDir);

            // Link against import libs
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, "Spout.lib"));
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, "SpoutDX12.lib"));

            // Delay-load by DLL names only (no paths).
            PublicDelayLoadDLLs.AddRange(new[] { "Spout.dll", "SpoutDX12.dll" });

            // Place alongside built target binaries
            RuntimeDependencies.Add("$(BinaryOutputDir)/Spout.dll",
                    Path.Combine(BinDir, "Spout.dll"));
            RuntimeDependencies.Add("$(BinaryOutputDir)/SpoutDX12.dll",
                    Path.Combine(BinDir, "SpoutDX12.dll"));
            // Stage both for packaged builds
            RuntimeDependencies.Add(Path.Combine(BinDir, "Spout.dll"), StagedFileType.NonUFS);
            RuntimeDependencies.Add(Path.Combine(BinDir, "SpoutDX12.dll"), StagedFileType.NonUFS);
        }
    }
}
