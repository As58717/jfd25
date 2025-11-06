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

        PublicIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "Public"),
        });

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
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "D3D11RHI",
                "D3D12RHI"
            });

            bool bWithNvenc = false;

            string nvencDirectory = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../ThirdParty/NVENC"));
            string interfaceDirectory = Path.Combine(nvencDirectory, "Interface");
            string libDirectory = Path.Combine(nvencDirectory, "Lib", "Win64");

            string nvencHeader = Path.Combine(interfaceDirectory, "nvEncodeAPI.h");
            string nvencLib = Path.Combine(libDirectory, "nvencodeapi.lib");
            string nvcuvidLib = Path.Combine(libDirectory, "nvcuvid.lib");

            if (File.Exists(nvencHeader) && File.Exists(nvencLib) && File.Exists(nvcuvidLib))
            {
                bWithNvenc = true;

                PublicIncludePaths.Add(interfaceDirectory);
                PublicSystemIncludePaths.Add(interfaceDirectory);

                PublicAdditionalLibraries.Add(nvencLib);
                PublicAdditionalLibraries.Add(nvcuvidLib);

                PublicDelayLoadDLLs.Add("nvEncodeAPI64.dll");
            }
            else
            {
                System.Console.WriteLine("OmniCapture: NVENC SDK not found – NVENC support will be disabled.");
            }

            PublicAdditionalLibraries.Add("d3d11.lib");

            PrivateDefinitions.Add($"WITH_OMNI_NVENC={(bWithNvenc ? 1 : 0)}");

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
