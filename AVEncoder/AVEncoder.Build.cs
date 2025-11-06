// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System;

[SupportedPlatforms("Linux")]
[SupportedPlatformGroups("Windows")]
public class AVEncoder : ModuleRules
{
	public AVEncoder(ReadOnlyTargetRules Target) : base(Target)
	{
		// Without these two compilation fails on VS2017 with D8049: command line is too long to fit in debug record.
		bLegacyPublicIncludePaths = false;
		DefaultBuildSettings = BuildSettingsVersion.V2;

		// PCHUsage = PCHUsageMode.NoPCHs;

		// PrecompileForTargets = PrecompileTargetsType.None;

		PublicIncludePaths.AddRange(new string[] {
			Path.Combine(ModuleDirectory, "Public")
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Engine",
			"SignalProcessing"
		});

		PublicDependencyModuleNames.AddRange(new string[] {
			"RenderCore",
			"Core",
			"RHI",
			"CUDA"
			// ... add other public dependencies that you statically link with here ...
		});

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows) || Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PublicIncludePathModuleNames.Add("Vulkan");
		}

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"D3D11RHI",
				"D3D12RHI"
			});

                        AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11", "DX12");

                        string ModuleRoot = ModuleDirectory;
                        string InterfacePath = Path.Combine(ModuleRoot, "..", "Interface");
                        if (Directory.Exists(InterfacePath))
                        {
                                PublicIncludePaths.Add(InterfacePath);
                                PublicSystemIncludePaths.Add(InterfacePath);
                        }

                        string LibPath = Path.Combine(ModuleRoot, "..", "Lib", "x64");
                        if (Directory.Exists(LibPath))
                        {
                                PublicAdditionalLibraries.Add(Path.Combine(LibPath, "nvencodeapi.lib"));
                                PublicAdditionalLibraries.Add(Path.Combine(LibPath, "nvcuvid.lib"));
                        }

                        PublicSystemLibraries.AddRange(new string[] {
                                "mfplat.lib",
                                "mfuuid.lib",
                        });

                        PublicDelayLoadDLLs.Add("Mfreadwrite.dll");

                        PublicDelayLoadDLLs.Add("nvEncodeAPI64.dll");

                        PrivateDefinitions.Add("AVENCODER_VIDEO_ENCODER_AVAILABLE_NVENC=1");
                }
        }
}
