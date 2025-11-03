using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using UnrealBuildTool;

public class OmniCapture : ModuleRules
{
    public OmniCapture(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.NoPCHs;
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "RenderCore",
            "RHI",
            "Projects",
            "Slate",
            "SlateCore",
            "UMG",
            "ImageWriteQueue",
            "Json",
            "JsonUtilities"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "ApplicationCore",
            "AudioExtensions",
            "AudioMixer",
            "CinematicCamera",
            "DeveloperSettings",
            "HeadMountedDisplay",
            "ImageCore",
            "ImageWrapper",
            "InputCore",
            "MediaUtils",
            "Renderer"
        });

        AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib", "UElibPNG");

        bool bHasOpenEXR = false;
        HashSet<string> OpenExrModules = new HashSet<string>();
        HashSet<string> ImathModules = new HashSet<string>();

        string thirdPartyDirectory = Target.UEThirdPartySourceDirectory;
        if (!string.IsNullOrEmpty(thirdPartyDirectory) && Directory.Exists(thirdPartyDirectory))
        {
            CollectThirdPartyModules(thirdPartyDirectory, "OpenEXR", OpenExrModules);
            CollectThirdPartyModules(thirdPartyDirectory, "OpenExr", OpenExrModules);
            CollectThirdPartyModules(thirdPartyDirectory, "Imath", ImathModules);
        }

        if (OpenExrModules.Count > 0)
        {
            bHasOpenEXR = true;
            AddEngineThirdPartyPrivateStaticDependencies(Target, OpenExrModules.ToArray());

            if (ImathModules.Count > 0)
            {
                AddEngineThirdPartyPrivateStaticDependencies(Target, ImathModules.ToArray());
            }
        }

        PrivateDefinitions.Add($"WITH_OMNICAPTURE_OPENEXR={(bHasOpenEXR ? 1 : 0)}");

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDependencyModuleNames.Add("AVEncoder");

            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "D3D11RHI",
                "D3D12RHI"
            });

            PrivateDefinitions.Add("WITH_OMNI_NVENC=1");

            // Ensure the expected project binaries directory exists before the linker writes outputs.
            if (Target.ProjectFile != null)
            {
                string projectBinariesDirectory = Path.Combine(Target.ProjectFile.Directory.FullName, "Binaries", "Win64");
                if (!Directory.Exists(projectBinariesDirectory))
                {
                    Directory.CreateDirectory(projectBinariesDirectory);
                }
            }
        }
        else
        {
            PrivateDefinitions.Add("WITH_OMNI_NVENC=0");
        }
    }

    private static void CollectThirdPartyModules(string thirdPartyDirectory, string filePrefix, HashSet<string> moduleNames)
    {
        foreach (string buildFile in Directory.GetFiles(thirdPartyDirectory, $"{filePrefix}*.Build.cs", SearchOption.AllDirectories))
        {
            string moduleName = ExtractModuleName(buildFile);
            if (!string.IsNullOrEmpty(moduleName))
            {
                moduleNames.Add(moduleName);
            }
        }
    }

    private static string ExtractModuleName(string buildFile)
    {
        foreach (string line in File.ReadLines(buildFile))
        {
            Match match = Regex.Match(line, @"^\s*class\s+([A-Za-z0-9_]+)\s*:\s*ModuleRules");
            if (match.Success)
            {
                return match.Groups[1].Value;
            }
        }

        return string.Empty;
    }
}
