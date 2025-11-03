#include "OmniCaptureNVENCEncoder.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "OmniCaptureTypes.h"
#include "Misc/ScopeLock.h"
#include "Math/UnrealMathUtility.h"
#include "PixelFormat.h"
#include "RHI.h"
#include "UObject/UnrealType.h"
#include "Logging/LogMacros.h"
#if __has_include("RHIAdapter.h")
#include "RHIAdapter.h"
#define OMNI_HAS_RHI_ADAPTER 1
#else
#define OMNI_HAS_RHI_ADAPTER 0
#endif
#include "GenericPlatform/GenericPlatformDriver.h"
#include "Interfaces/IPluginManager.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#include <windows.h>
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogOmniCaptureNVENC, Log, All);

#if OMNI_WITH_AVENCODER
#include "RHIResources.h"
#include "RHICommandList.h"
#endif

namespace
{
#if PLATFORM_WINDOWS
    #define OMNI_NVENCAPI __stdcall
#else
    #define OMNI_NVENCAPI
#endif

#if OMNI_WITH_AVENCODER
    OmniAVEncoder::EVideoFormat ToVideoFormat(EOmniCaptureColorFormat Format)
    {
        switch (Format)
        {
        case EOmniCaptureColorFormat::NV12:
            return OmniAVEncoder::EVideoFormat::NV12;
        case EOmniCaptureColorFormat::P010:
            return OmniAVEncoder::EVideoFormat::P010;
        case EOmniCaptureColorFormat::BGRA:
        default:
            return OmniAVEncoder::EVideoFormat::BGRA8;
        }
    }

    struct FNVENCHardwareProbeResult
    {
        bool bDllPresent = false;
        bool bApisReady = false;
        bool bSessionOpenable = false;
        bool bSupportsH264 = false;
        bool bSupportsHEVC = false;
        bool bSupportsNV12 = false;
        bool bSupportsP010 = false;
        bool bSupportsBGRA = false;
        FString DllFailureReason;
        FString ApiFailureReason;
        FString SessionFailureReason;
        FString CodecFailureReason;
        FString NV12FailureReason;
        FString P010FailureReason;
        FString BGRAFailureReason;
        FString HardwareFailureReason;
    };

    using FNvEncodeAPIGetMaxSupportedVersion = uint32(OMNI_NVENCAPI*)(uint32*);

    FCriticalSection& GetProbeCacheMutex()
    {
        static FCriticalSection Mutex;
        return Mutex;
    }

    FCriticalSection& GetDllOverrideMutex()
    {
        static FCriticalSection Mutex;
        return Mutex;
    }

    FCriticalSection& GetModuleOverrideMutex()
    {
        static FCriticalSection Mutex;
        return Mutex;
    }

    bool& GetProbeValidFlag()
    {
        static bool bValid = false;
        return bValid;
    }

    FNVENCHardwareProbeResult& GetCachedProbe()
    {
        static FNVENCHardwareProbeResult Cached;
        return Cached;
    }

    FString& GetDllOverridePath()
    {
        static FString OverridePath;
        return OverridePath;
    }

    FString& GetModuleOverridePath()
    {
        static FString OverridePath;
        return OverridePath;
    }

    bool& GetAutoDetectModuleAttempted()
    {
        static bool bAttempted = false;
        return bAttempted;
    }

    FString& GetAutoDetectedModulePath()
    {
        static FString CachedPath;
        return CachedPath;
    }

    bool& GetAutoDetectDllAttempted()
    {
        static bool bAttempted = false;
        return bAttempted;
    }

    FString& GetAutoDetectedDllPath()
    {
        static FString CachedPath;
        return CachedPath;
    }

    FString NormalizePath(const FString& InPath)
    {
        FString Result = InPath;
        Result.TrimStartAndEndInline();
        if (!Result.IsEmpty())
        {
            Result = FPaths::ConvertRelativePathToFull(Result);
            FPaths::MakePlatformFilename(Result);
        }
        return Result;
    }

#if PLATFORM_WINDOWS
    FString GetSystem32DirectoryFromAPI()
    {
        TCHAR Buffer[MAX_PATH] = { 0 };
        const UINT Length = ::GetSystemDirectory(Buffer, UE_ARRAY_COUNT(Buffer));
        if (Length > 0 && Length < UE_ARRAY_COUNT(Buffer))
        {
            return NormalizePath(FString(Buffer));
        }
        return FString();
    }

    FString GetSysWow64DirectoryFromAPI()
    {
#if PLATFORM_64BITS
        TCHAR Buffer[MAX_PATH] = { 0 };
        const UINT Length = ::GetSystemWow64Directory(Buffer, UE_ARRAY_COUNT(Buffer));
        if (Length > 0 && Length < UE_ARRAY_COUNT(Buffer))
        {
            return NormalizePath(FString(Buffer));
        }
#endif
        return FString();
    }
#endif // PLATFORM_WINDOWS

#if OMNI_WITH_AVENCODER && PLATFORM_WINDOWS
    void AddUniqueDirectory(TArray<FString>& Directories, const FString& Directory)
    {
        const FString Normalized = NormalizePath(Directory);
        if (!Normalized.IsEmpty())
        {
            Directories.AddUnique(Normalized);
        }
    }

    bool DirectoryContainsAVEncoderBinary(const FString& Directory)
    {
        if (Directory.IsEmpty())
        {
            return false;
        }

        const FString Normalized = NormalizePath(Directory);
        if (Normalized.IsEmpty() || !FPaths::DirectoryExists(Normalized))
        {
            return false;
        }

        TArray<FString> CandidateFiles;
        IFileManager::Get().FindFiles(CandidateFiles, *(FPaths::Combine(Normalized, TEXT("*.dll"))), true, false);
        for (const FString& FileName : CandidateFiles)
        {
            if (FileName.Contains(TEXT("AVEncoder"), ESearchCase::IgnoreCase))
            {
                return true;
            }
        }

        return false;
    }

    FString DetectAVEncoderModuleDirectory()
    {
        if (GetAutoDetectModuleAttempted())
        {
            return GetAutoDetectedModulePath();
        }

        GetAutoDetectModuleAttempted() = true;
        FString& CachedPath = GetAutoDetectedModulePath();

        TArray<FString> CandidateDirectories;
        AddUniqueDirectory(CandidateDirectories, FPlatformProcess::ExecutableDir());
        AddUniqueDirectory(CandidateDirectories, FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Win64")));
        AddUniqueDirectory(CandidateDirectories, FPaths::Combine(FPaths::EngineDir(), TEXT("Plugins/Media/AVEncoder/Binaries/Win64")));
        AddUniqueDirectory(CandidateDirectories, FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Win64")));

        if (IPluginManager::Get().IsInitialized())
        {
            if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AVEncoder")))
            {
                AddUniqueDirectory(CandidateDirectories, FPaths::Combine(Plugin->GetBaseDir(), TEXT("Binaries/Win64")));
            }
        }

        for (const FString& Candidate : CandidateDirectories)
        {
            if (DirectoryContainsAVEncoderBinary(Candidate))
            {
                CachedPath = Candidate;
                return CachedPath;
            }
        }

        CachedPath.Reset();
        return CachedPath;
    }

    FString CheckDirectoryForNVENCDll(const FString& Directory)
    {
        if (Directory.IsEmpty())
        {
            return FString();
        }

        const FString Normalized = NormalizePath(Directory);
        if (Normalized.IsEmpty())
        {
            return FString();
        }

        if (FPaths::GetExtension(Normalized, true).Equals(TEXT(".dll"), ESearchCase::IgnoreCase))
        {
            return FPaths::FileExists(Normalized) ? Normalized : FString();
        }

        FString Candidate = FPaths::Combine(Normalized, TEXT("nvEncodeAPI64.dll"));
        FPaths::MakePlatformFilename(Candidate);
        return FPaths::FileExists(Candidate) ? Candidate : FString();
    }

    FString DetectNVENCDllPath()
    {
        if (GetAutoDetectDllAttempted())
        {
            return GetAutoDetectedDllPath();
        }

        GetAutoDetectDllAttempted() = true;
        FString& CachedPath = GetAutoDetectedDllPath();

        TArray<FString> CandidateDirectories;
        AddUniqueDirectory(CandidateDirectories, FPlatformProcess::ExecutableDir());
        AddUniqueDirectory(CandidateDirectories, FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/Win64")));
        AddUniqueDirectory(CandidateDirectories, FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Win64")));

        const FString SystemRoot = NormalizePath(FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")));
        if (!SystemRoot.IsEmpty())
        {
            AddUniqueDirectory(CandidateDirectories, FPaths::Combine(SystemRoot, TEXT("System32")));
            AddUniqueDirectory(CandidateDirectories, FPaths::Combine(SystemRoot, TEXT("SysWOW64")));
        }

#if PLATFORM_WINDOWS
        const FString System32Directory = GetSystem32DirectoryFromAPI();
        if (!System32Directory.IsEmpty())
        {
            AddUniqueDirectory(CandidateDirectories, System32Directory);
        }

        const FString SysWow64Directory = GetSysWow64DirectoryFromAPI();
        if (!SysWow64Directory.IsEmpty())
        {
            AddUniqueDirectory(CandidateDirectories, SysWow64Directory);
        }
#endif

        for (const FString& Directory : CandidateDirectories)
        {
            const FString FoundDll = CheckDirectoryForNVENCDll(Directory);
            if (!FoundDll.IsEmpty())
            {
                CachedPath = FoundDll;
                return CachedPath;
            }
        }

        if (!SystemRoot.IsEmpty())
        {
            const FString DriverStore = FPaths::Combine(SystemRoot, TEXT("System32/DriverStore/FileRepository"));
            if (FPaths::DirectoryExists(DriverStore))
            {
                TArray<FString> FoundDlls;
                IFileManager::Get().FindFilesRecursive(FoundDlls, *DriverStore, TEXT("nvencodeapi64.dll"), true, false);
                if (FoundDlls.Num() > 0)
                {
                    FoundDlls.Sort([](const FString& A, const FString& B)
                    {
                        return A > B;
                    });
                    FString FirstMatch = NormalizePath(FoundDlls[0]);
                    if (!FirstMatch.IsEmpty())
                    {
                        CachedPath = FirstMatch;
                        return CachedPath;
                    }
                }
            }
        }

        const TCHAR* ProgramFilesVars[] = { TEXT("ProgramFiles"), TEXT("ProgramFiles(x86)") };
        for (const TCHAR* EnvVar : ProgramFilesVars)
        {
            const FString Root = NormalizePath(FPlatformMisc::GetEnvironmentVariable(EnvVar));
            if (Root.IsEmpty())
            {
                continue;
            }

            const FString NvidiaRoot = FPaths::Combine(Root, TEXT("NVIDIA Corporation"));
            if (FPaths::DirectoryExists(NvidiaRoot))
            {
                TArray<FString> FoundDlls;
                IFileManager::Get().FindFilesRecursive(FoundDlls, *NvidiaRoot, TEXT("nvencodeapi64.dll"), true, false);
                if (FoundDlls.Num() > 0)
                {
                    FoundDlls.Sort([](const FString& A, const FString& B)
                    {
                        return A > B;
                    });
                    FString FirstMatch = NormalizePath(FoundDlls[0]);
                    if (!FirstMatch.IsEmpty())
                    {
                        CachedPath = FirstMatch;
                        return CachedPath;
                    }
                }
            }
        }

        CachedPath.Reset();
        return CachedPath;
    }
#else
    FString DetectAVEncoderModuleDirectory()
    {
        if (!GetAutoDetectModuleAttempted())
        {
            GetAutoDetectModuleAttempted() = true;
            GetAutoDetectedModulePath().Reset();
        }
        return GetAutoDetectedModulePath();
    }

    FString DetectNVENCDllPath()
    {
        if (!GetAutoDetectDllAttempted())
        {
            GetAutoDetectDllAttempted() = true;
            GetAutoDetectedDllPath().Reset();
        }
        return GetAutoDetectedDllPath();
    }
#endif // OMNI_WITH_AVENCODER && PLATFORM_WINDOWS

    bool& GetModuleDirectoryRegisteredFlag()
    {
        static bool bRegistered = false;
        return bRegistered;
    }

    void EnsureModuleOverrideRegistered()
    {
        FString OverridePath;
        bool bNeedsRegistration = false;

        {
            FScopeLock OverrideLock(&GetModuleOverrideMutex());
            OverridePath = GetModuleOverridePath();
            bool& bRegistered = GetModuleDirectoryRegisteredFlag();
            if (!OverridePath.IsEmpty() && !bRegistered)
            {
                bNeedsRegistration = true;
                bRegistered = true;
            }
        }

        if (bNeedsRegistration)
        {
            FModuleManager::Get().AddModuleDirectory(*OverridePath);
        }
    }

    bool TryCreateEncoderSession(OmniAVEncoder::ECodec Codec, OmniAVEncoder::EVideoFormat Format, FString& OutFailureReason)
    {
        constexpr int32 TestWidth = 256;
        constexpr int32 TestHeight = 144;

        OmniAVEncoder::FVideoEncoderInput::FCreateParameters CreateParameters;
        CreateParameters.Width = TestWidth;
        CreateParameters.Height = TestHeight;
        CreateParameters.MaxBufferDimensions = FIntPoint(TestWidth, TestHeight);
        CreateParameters.Format = Format;
        CreateParameters.DebugName = TEXT("OmniNVENCProbe");
        CreateParameters.bAutoCopy = true;

        TSharedPtr<OmniAVEncoder::FVideoEncoderInput> EncoderInput = OmniAVEncoder::FVideoEncoderInput::CreateForRHI(CreateParameters);
        if (!EncoderInput.IsValid())
        {
            OutFailureReason = TEXT("Failed to create AVEncoder input for probe");
            return false;
        }

        OmniAVEncoder::FVideoEncoder::FLayerConfig LayerConfig;
        LayerConfig.Width = TestWidth;
        LayerConfig.Height = TestHeight;
        LayerConfig.MaxFramerate = 60;
        LayerConfig.TargetBitrate = 5 * 1000 * 1000;
        LayerConfig.MaxBitrate = 10 * 1000 * 1000;

        OmniAVEncoder::FVideoEncoder::FCodecConfig CodecConfig;
        CodecConfig.GOPLength = 30;
        CodecConfig.MaxNumBFrames = 0;
        CodecConfig.bEnableFrameReordering = false;

        OmniAVEncoder::FVideoEncoder::FInit EncoderInit;
        EncoderInit.Codec = Codec;
        EncoderInit.CodecConfig = CodecConfig;
        EncoderInit.Layers.Add(LayerConfig);

        auto OnEncodedPacket = OmniAVEncoder::FVideoEncoder::FOnEncodedPacket::CreateLambda([](const OmniAVEncoder::FVideoEncoder::FEncodedPacket&)
        {
        });

        TSharedPtr<OmniAVEncoder::FVideoEncoder> VideoEncoder = OmniAVEncoder::FVideoEncoderFactory::Create(*EncoderInput, EncoderInit, MoveTemp(OnEncodedPacket));
        if (!VideoEncoder.IsValid())
        {
            OutFailureReason = TEXT("Failed to create AVEncoder NVENC instance");
            return false;
        }

        VideoEncoder.Reset();
        EncoderInput.Reset();

        return true;
    }

    FNVENCHardwareProbeResult RunNVENCHardwareProbe()
    {
        FNVENCHardwareProbeResult Result;

        FString OverridePath;
        {
            FScopeLock OverrideLock(&GetDllOverrideMutex());
            OverridePath = GetDllOverridePath();
        }

        OverridePath.TrimStartAndEndInline();
        FString NormalizedOverridePath = OverridePath;
        if (!NormalizedOverridePath.IsEmpty())
        {
            NormalizedOverridePath = FPaths::ConvertRelativePathToFull(NormalizedOverridePath);
            FPaths::MakePlatformFilename(NormalizedOverridePath);
        }

        FString OverrideCandidate = NormalizedOverridePath;
        if (!OverrideCandidate.IsEmpty())
        {
            const FString Extension = FPaths::GetExtension(OverrideCandidate, true);
            if (!Extension.Equals(TEXT(".dll"), ESearchCase::IgnoreCase))
            {
                OverrideCandidate = FPaths::Combine(OverrideCandidate, TEXT("nvEncodeAPI64.dll"));
            }
            FPaths::MakePlatformFilename(OverrideCandidate);
        }

        void* NvencHandle = nullptr;
        FString LoadedFrom;
        TArray<FString> FailureMessages;

        if (!OverrideCandidate.IsEmpty())
        {
            if (!FPaths::FileExists(*OverrideCandidate))
            {
                FailureMessages.Add(FString::Printf(TEXT("Override path not found: %s."), *OverrideCandidate));
            }
            else
            {
                NvencHandle = FPlatformProcess::GetDllHandle(*OverrideCandidate);
                if (NvencHandle)
                {
                    LoadedFrom = OverrideCandidate;
                }
                else
                {
                    FailureMessages.Add(FString::Printf(TEXT("Failed to load override DLL: %s."), *OverrideCandidate));
                }
            }
        }

        if (!NvencHandle)
        {
            NvencHandle = FPlatformProcess::GetDllHandle(TEXT("nvEncodeAPI64.dll"));
            if (NvencHandle)
            {
                LoadedFrom = TEXT("system search paths");
            }
            else
            {
                FailureMessages.Add(TEXT("Failed to load nvEncodeAPI64.dll from system search paths."));
            }
        }

        if (NvencHandle)
        {
            Result.bDllPresent = true;
            if (!LoadedFrom.IsEmpty())
            {
                UE_LOG(LogTemp, Verbose, TEXT("NVENC probe loading nvEncodeAPI64.dll from %s"), *LoadedFrom);
            }
            FNvEncodeAPIGetMaxSupportedVersion NvEncodeAPIGetMaxSupportedVersion = reinterpret_cast<FNvEncodeAPIGetMaxSupportedVersion>(FPlatformProcess::GetDllExport(NvencHandle, TEXT("NvEncodeAPIGetMaxSupportedVersion")));
            if (NvEncodeAPIGetMaxSupportedVersion)
            {
                uint32 MaxVersion = 0;
                const uint32 NvStatus = NvEncodeAPIGetMaxSupportedVersion(&MaxVersion);
                if (NvStatus == 0 && MaxVersion != 0)
                {
                    Result.bApisReady = true;
                }
                else
                {
                    Result.ApiFailureReason = FString::Printf(TEXT("NvEncodeAPIGetMaxSupportedVersion failed (status=0x%08x, version=%u)"), NvStatus, MaxVersion);
                }
            }
            else
            {
                Result.ApiFailureReason = TEXT("NvEncodeAPIGetMaxSupportedVersion export missing in nvEncodeAPI64.dll");
            }
            FPlatformProcess::FreeDllHandle(NvencHandle);
        }
        else
        {
            Result.DllFailureReason = FailureMessages.Num() > 0
                ? FString::Join(FailureMessages, TEXT(" "))
                : TEXT("Failed to load nvEncodeAPI64.dll.");
        }

        if (!Result.bDllPresent)
        {
            Result.HardwareFailureReason = Result.DllFailureReason.IsEmpty() ? TEXT("NVENC runtime DLL missing") : Result.DllFailureReason;
            return Result;
        }

        if (!Result.bApisReady)
        {
            Result.HardwareFailureReason = Result.ApiFailureReason.IsEmpty() ? TEXT("Failed to query NVENC API version") : Result.ApiFailureReason;
            return Result;
        }

        EnsureModuleOverrideRegistered();

        if (!FModuleManager::Get().IsModuleLoaded(TEXT("AVEncoder")))
        {
            if (!FModuleManager::Get().LoadModule(TEXT("AVEncoder")))
            {
                Result.HardwareFailureReason = TEXT("Failed to load the AVEncoder module. Provide an override path if it resides outside the engine.");
                return Result;
            }
        }

        FString SessionFailure;
        if (!TryCreateEncoderSession(OmniAVEncoder::ECodec::H264, OmniAVEncoder::EVideoFormat::BGRA8, SessionFailure))
        {
            Result.SessionFailureReason = SessionFailure;
            Result.BGRAFailureReason = SessionFailure;
            Result.HardwareFailureReason = SessionFailure;
            return Result;
        }

        Result.bSessionOpenable = true;
        Result.bSupportsH264 = true;
        Result.bSupportsBGRA = true;

        FString Nv12Failure;
        if (TryCreateEncoderSession(OmniAVEncoder::ECodec::H264, OmniAVEncoder::EVideoFormat::NV12, Nv12Failure))
        {
            Result.bSupportsNV12 = true;
        }
        else
        {
            Result.NV12FailureReason = Nv12Failure;
        }

        bool bHevcSuccess = false;
        FString HevcFailure;
        if (TryCreateEncoderSession(OmniAVEncoder::ECodec::HEVC, OmniAVEncoder::EVideoFormat::NV12, HevcFailure))
        {
            Result.bSupportsHEVC = true;
            bHevcSuccess = true;
        }
        else
        {
            Result.CodecFailureReason = HevcFailure;
        }

        FString P010Failure;
        if (TryCreateEncoderSession(OmniAVEncoder::ECodec::HEVC, OmniAVEncoder::EVideoFormat::P010, P010Failure))
        {
            Result.bSupportsP010 = true;
            Result.bSupportsHEVC = true;
            bHevcSuccess = true;
        }
        else
        {
            Result.P010FailureReason = P010Failure;
        }

        if (!Result.bSupportsNV12 && Result.NV12FailureReason.IsEmpty())
        {
            Result.NV12FailureReason = TEXT("NV12 input format is not available on this NVENC hardware.");
        }

        if (!Result.bSupportsP010 && Result.P010FailureReason.IsEmpty())
        {
            Result.P010FailureReason = TEXT("10-bit P010 input is not available on this NVENC hardware.");
        }

        if (!bHevcSuccess)
        {
            if (!Result.P010FailureReason.IsEmpty())
            {
                Result.CodecFailureReason = Result.P010FailureReason;
            }
            Result.bSupportsHEVC = false;
        }
        else
        {
            Result.CodecFailureReason.Reset();
        }

        Result.HardwareFailureReason = TEXT("");

        UE_LOG(LogTemp, Log, TEXT("NVENC probe succeeded (NV12=%s, P010=%s, HEVC=%s, BGRA=%s)"),
            Result.bSupportsNV12 ? TEXT("Yes") : TEXT("No"),
            Result.bSupportsP010 ? TEXT("Yes") : TEXT("No"),
            Result.bSupportsHEVC ? TEXT("Yes") : TEXT("No"),
            Result.bSupportsBGRA ? TEXT("Yes") : TEXT("No"));
        return Result;
    }

    const FNVENCHardwareProbeResult& GetNVENCHardwareProbe()
    {
        FScopeLock CacheLock(&GetProbeCacheMutex());
        if (!GetProbeValidFlag())
        {
            GetCachedProbe() = RunNVENCHardwareProbe();
            GetProbeValidFlag() = true;
            const FNVENCHardwareProbeResult& CachedResult = GetCachedProbe();
            if (!CachedResult.bDllPresent || !CachedResult.bApisReady || !CachedResult.bSessionOpenable)
            {
                UE_LOG(LogTemp, Warning, TEXT("NVENC probe failed (Dll=%s, Api=%s, Session=%s). Reasons: %s | %s | %s"),
                    CachedResult.bDllPresent ? TEXT("Yes") : TEXT("No"),
                    CachedResult.bApisReady ? TEXT("Yes") : TEXT("No"),
                    CachedResult.bSessionOpenable ? TEXT("Yes") : TEXT("No"),
                    CachedResult.DllFailureReason.IsEmpty() ? TEXT("<none>") : *CachedResult.DllFailureReason,
                    CachedResult.ApiFailureReason.IsEmpty() ? TEXT("<none>") : *CachedResult.ApiFailureReason,
                    CachedResult.SessionFailureReason.IsEmpty() ? TEXT("<none>") : *CachedResult.SessionFailureReason);
            }
        }
        return GetCachedProbe();
    }
#endif
}
#if PLATFORM_WINDOWS
#undef OMNI_NVENCAPI
#endif

FOmniCaptureNVENCEncoder::FOmniCaptureNVENCEncoder()
{
}

bool FOmniCaptureNVENCEncoder::IsNVENCAvailable()
{
#if OMNI_WITH_AVENCODER && PLATFORM_WINDOWS
    const FNVENCHardwareProbeResult& Probe = GetNVENCHardwareProbe();
    return Probe.bDllPresent && Probe.bApisReady && Probe.bSessionOpenable;
#else
    return false;
#endif
}

FOmniNVENCCapabilities FOmniCaptureNVENCEncoder::QueryCapabilities()
{
    FOmniNVENCCapabilities Caps;

#if OMNI_WITH_AVENCODER && PLATFORM_WINDOWS
    const FNVENCHardwareProbeResult& Probe = GetNVENCHardwareProbe();

    Caps.bDllPresent = Probe.bDllPresent;
    Caps.bApisReady = Probe.bApisReady;
    Caps.bSessionOpenable = Probe.bSessionOpenable;

    const bool bEngineSupportsNV12 = SupportsColorFormat(EOmniCaptureColorFormat::NV12);
    const bool bEngineSupportsP010 = SupportsColorFormat(EOmniCaptureColorFormat::P010);
    const bool bEngineSupportsBGRA = SupportsColorFormat(EOmniCaptureColorFormat::BGRA);

    Caps.bSupportsHEVC = Probe.bSupportsHEVC;
    Caps.bSupportsNV12 = Probe.bSupportsNV12 && bEngineSupportsNV12;
    Caps.bSupportsP010 = Probe.bSupportsP010 && bEngineSupportsP010;
    Caps.bSupportsBGRA = Probe.bSupportsBGRA && bEngineSupportsBGRA;
    Caps.bSupports10Bit = Caps.bSupportsP010;
    Caps.bHardwareAvailable = Caps.bDllPresent && Caps.bApisReady && Caps.bSessionOpenable;

    Caps.DllFailureReason = Probe.DllFailureReason;
    Caps.ApiFailureReason = Probe.ApiFailureReason;
    Caps.SessionFailureReason = Probe.SessionFailureReason;
    Caps.CodecFailureReason = Probe.CodecFailureReason;
    Caps.NV12FailureReason = Probe.NV12FailureReason;
    Caps.P010FailureReason = Probe.P010FailureReason;
    Caps.BGRAFailureReason = Probe.BGRAFailureReason;
    Caps.HardwareFailureReason = Probe.HardwareFailureReason;

    if (!Caps.bSupportsNV12)
    {
        if (Probe.bSupportsNV12 && !bEngineSupportsNV12)
        {
            Caps.NV12FailureReason = TEXT("NV12 pixel format unsupported by this engine build or active RHI.");
        }
        else if (Caps.NV12FailureReason.IsEmpty())
        {
            Caps.NV12FailureReason = TEXT("NV12 input format is not available on this NVENC hardware.");
        }
    }

    if (!Caps.bSupportsP010)
    {
        if (Probe.bSupportsP010 && !bEngineSupportsP010)
        {
            Caps.P010FailureReason = TEXT("P010 pixel format unsupported by this engine build or active RHI.");
        }
        else if (Caps.P010FailureReason.IsEmpty())
        {
            Caps.P010FailureReason = TEXT("10-bit P010 input is not available on this NVENC hardware.");
        }
    }

    if (!Caps.bSupportsBGRA && Caps.BGRAFailureReason.IsEmpty() && Probe.bSupportsBGRA)
    {
        Caps.BGRAFailureReason = TEXT("BGRA input is not available with the detected NVENC runtime.");
    }
#else
    Caps.bHardwareAvailable = false;
    Caps.DllFailureReason = TEXT("NVENC support is only available on Windows builds with AVEncoder.");
    Caps.HardwareFailureReason = Caps.DllFailureReason;
#endif

    Caps.AdapterName = FPlatformMisc::GetPrimaryGPUBrand();
#if PLATFORM_WINDOWS
    FString DeviceDescription;
    FString PrimaryBrand = FPlatformMisc::GetPrimaryGPUBrand();
#if OMNI_HAS_RHI_ADAPTER
    if (GDynamicRHI)
    {
        FRHIAdapterInfo AdapterInfo;
        GDynamicRHI->RHIGetAdapterInfo(AdapterInfo);
        DeviceDescription = AdapterInfo.Description;
    }
#else
    DeviceDescription = PrimaryBrand;
#endif
    if (DeviceDescription.IsEmpty())
    {
        DeviceDescription = PrimaryBrand;
    }
    const FGPUDriverInfo DriverInfo = FPlatformMisc::GetGPUDriverInfo(DeviceDescription);
#if UE_VERSION_NEWER_THAN(5, 5, 0)
    Caps.DriverVersion = DriverInfo.UserDriverVersion;
#else
    Caps.DriverVersion = DriverInfo.DriverVersion;
#endif
#endif

    return Caps;
}
#undef OMNI_HAS_RHI_ADAPTER

bool FOmniCaptureNVENCEncoder::SupportsColorFormat(EOmniCaptureColorFormat Format)
{
#if OMNI_WITH_AVENCODER
    switch (Format)
    {
    case EOmniCaptureColorFormat::NV12:
        return GPixelFormats[PF_NV12].Supported != 0;
    case EOmniCaptureColorFormat::P010:
#if defined(PF_P010)
        return GPixelFormats[PF_P010].Supported != 0;
#else
        return false;
#endif
    case EOmniCaptureColorFormat::BGRA:
        return GPixelFormats[PF_B8G8R8A8].Supported != 0;
    default:
        return false;
    }
#else
    return Format == EOmniCaptureColorFormat::BGRA;
#endif
}

bool FOmniCaptureNVENCEncoder::SupportsZeroCopyRHI()
{
#if PLATFORM_WINDOWS
    return GDynamicRHI &&
        (GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D11 ||
         GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D12);
#else
    return false;
#endif
}

void FOmniCaptureNVENCEncoder::SetModuleOverridePath(const FString& InOverridePath)
{
#if OMNI_WITH_AVENCODER
    FString NormalizedPath = InOverridePath;
    NormalizedPath.TrimStartAndEndInline();
    if (!NormalizedPath.IsEmpty())
    {
        NormalizedPath = FPaths::ConvertRelativePathToFull(NormalizedPath);
        FPaths::MakePlatformFilename(NormalizedPath);

        if (FPaths::FileExists(NormalizedPath))
        {
            NormalizedPath = FPaths::GetPath(NormalizedPath);
            FPaths::MakePlatformFilename(NormalizedPath);
        }
        else if (FPaths::DirectoryExists(NormalizedPath))
        {
#if PLATFORM_WINDOWS
            if (!DirectoryContainsAVEncoderBinary(NormalizedPath))
            {
                const FString PlatformSubdir = FPlatformProcess::GetBinariesSubdirectory();
                const FString BinariesPath = FPaths::Combine(NormalizedPath, TEXT("Binaries"), PlatformSubdir);
                if (DirectoryContainsAVEncoderBinary(BinariesPath))
                {
                    NormalizedPath = BinariesPath;
                }
                else
                {
                    const FString PlatformPath = FPaths::Combine(NormalizedPath, PlatformSubdir);
                    if (DirectoryContainsAVEncoderBinary(PlatformPath))
                    {
                        NormalizedPath = PlatformPath;
                    }
                }
            }
            FPaths::MakePlatformFilename(NormalizedPath);
#endif
        }
    }
    else
    {
        NormalizedPath = DetectAVEncoderModuleDirectory();
    }

    const bool bHasPath = !NormalizedPath.IsEmpty();
    bool bChanged = false;
    {
        FScopeLock OverrideLock(&GetModuleOverrideMutex());
        FString& StoredPath = GetModuleOverridePath();
        if (!StoredPath.Equals(NormalizedPath, ESearchCase::CaseSensitive))
        {
            StoredPath = NormalizedPath;
            GetModuleDirectoryRegisteredFlag() = false;
            bChanged = true;
        }
    }

    if (bChanged)
    {
        if (bHasPath)
        {
            EnsureModuleOverrideRegistered();
        }
        else
        {
            FScopeLock OverrideLock(&GetModuleOverrideMutex());
            GetModuleDirectoryRegisteredFlag() = false;
        }

        FScopeLock CacheLock(&GetProbeCacheMutex());
        GetProbeValidFlag() = false;
    }
#else
    if (!InOverridePath.IsEmpty())
    {
        UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("Ignoring AVEncoder module override '%s' because NVENC support was compiled out."), *InOverridePath);
    }
    else
    {
        UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("AVEncoder module override reset ignored because NVENC support was compiled out."));
    }
#endif
}

void FOmniCaptureNVENCEncoder::SetDllOverridePath(const FString& InOverridePath)
{
#if OMNI_WITH_AVENCODER
    FString NormalizedPath = InOverridePath;
    NormalizedPath.TrimStartAndEndInline();
    if (!NormalizedPath.IsEmpty())
    {
        NormalizedPath = FPaths::ConvertRelativePathToFull(NormalizedPath);
        FPaths::MakePlatformFilename(NormalizedPath);
    }
    else
    {
        NormalizedPath = DetectNVENCDllPath();
    }

    bool bChanged = false;
    {
        FScopeLock OverrideLock(&GetDllOverrideMutex());
        if (!GetDllOverridePath().Equals(NormalizedPath, ESearchCase::CaseSensitive))
        {
            GetDllOverridePath() = NormalizedPath;
            bChanged = true;
        }
    }

    if (bChanged)
    {
        FScopeLock CacheLock(&GetProbeCacheMutex());
        GetProbeValidFlag() = false;
    }
#else
    if (!InOverridePath.IsEmpty())
    {
        UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("Ignoring NVENC DLL override '%s' because NVENC support was compiled out."), *InOverridePath);
    }
    else
    {
        UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("NVENC DLL override reset ignored because NVENC support was compiled out."));
    }
#endif
}

void FOmniCaptureNVENCEncoder::InvalidateCachedCapabilities()
{
#if OMNI_WITH_AVENCODER
    FScopeLock CacheLock(&GetProbeCacheMutex());
    GetProbeValidFlag() = false;

    {
        FScopeLock OverrideLock(&GetModuleOverrideMutex());
        GetAutoDetectModuleAttempted() = false;
        GetAutoDetectedModulePath().Reset();
    }

    {
        FScopeLock OverrideLock(&GetDllOverrideMutex());
        GetAutoDetectDllAttempted() = false;
        GetAutoDetectedDllPath().Reset();
    }
#endif

#if !OMNI_WITH_AVENCODER
    UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("Ignoring NVENC capability invalidation request because NVENC support was compiled out."));
#endif
}

FOmniCaptureNVENCEncoder::~FOmniCaptureNVENCEncoder()
{
    Finalize();
}

void FOmniCaptureNVENCEncoder::Initialize(const FOmniCaptureSettings& Settings, const FString& OutputDirectory)
{
    LastErrorMessage.Reset();
    FString Directory = OutputDirectory.IsEmpty() ? (FPaths::ProjectSavedDir() / TEXT("OmniCaptures")) : OutputDirectory;
    Directory = FPaths::ConvertRelativePathToFull(Directory);
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    PlatformFile.CreateDirectoryTree(*Directory);

    RequestedCodec = Settings.Codec;
    const bool bUseHEVC = RequestedCodec == EOmniCaptureCodec::HEVC;
    OutputFilePath = Directory / (Settings.OutputFileName + (bUseHEVC ? TEXT(".h265") : TEXT(".h264")));
    ColorFormat = Settings.NVENCColorFormat;
    bZeroCopyRequested = Settings.bZeroCopy;

#if OMNI_WITH_AVENCODER
    const FIntPoint OutputSize = Settings.GetOutputResolution();
    const int32 OutputWidth = OutputSize.X;
    const int32 OutputHeight = OutputSize.Y;

    EnsureModuleOverrideRegistered();

    if (!FModuleManager::Get().IsModuleLoaded(TEXT("AVEncoder")))
    {
        if (!FModuleManager::Get().LoadModule(TEXT("AVEncoder")))
        {
            LastErrorMessage = TEXT("Failed to load the AVEncoder module. Configure the module override path if it lives outside the engine.");
            UE_LOG(LogTemp, Error, TEXT("%s"), *LastErrorMessage);
            return;
        }
    }

    OmniAVEncoder::FVideoEncoderInput::FCreateParameters CreateParameters;
    CreateParameters.Width = OutputWidth;
    CreateParameters.Height = OutputHeight;
    CreateParameters.Format = ToVideoFormat(ColorFormat);
    CreateParameters.MaxBufferDimensions = FIntPoint(OutputWidth, OutputHeight);
    CreateParameters.DebugName = TEXT("OmniCaptureNVENC");
    CreateParameters.bAutoCopy = !bZeroCopyRequested;

    EncoderInput = OmniAVEncoder::FVideoEncoderInput::CreateForRHI(CreateParameters);
    if (!EncoderInput.IsValid())
    {
        const FString FormatName = StaticEnum<EOmniCaptureColorFormat>()->GetNameStringByValue(static_cast<int64>(ColorFormat));
        LastErrorMessage = FString::Printf(TEXT("Failed to create NVENC encoder input for %dx%d %s frames."),
            OutputWidth,
            OutputHeight,
            *FormatName);
        UE_LOG(LogTemp, Error, TEXT("%s"), *LastErrorMessage);
        return;
    }

    LayerConfig = OmniAVEncoder::FVideoEncoder::FLayerConfig();
    LayerConfig.Width = OutputWidth;
    LayerConfig.Height = OutputHeight;
    LayerConfig.MaxFramerate = 120;
    LayerConfig.TargetBitrate = Settings.Quality.TargetBitrateKbps * 1000;
    LayerConfig.MaxBitrate = FMath::Max<int32>(LayerConfig.TargetBitrate, Settings.Quality.MaxBitrateKbps * 1000);
    LayerConfig.MinQp = 0;
    LayerConfig.MaxQp = 51;

    CodecConfig = OmniAVEncoder::FVideoEncoder::FCodecConfig();
    CodecConfig.bLowLatency = Settings.Quality.bLowLatency;
    CodecConfig.GOPLength = Settings.Quality.GOPLength;
    CodecConfig.MaxNumBFrames = Settings.Quality.BFrames;
    CodecConfig.bEnableFrameReordering = Settings.Quality.BFrames > 0;

    OmniAVEncoder::FVideoEncoder::FInit EncoderInit;
    EncoderInit.Codec = bUseHEVC ? OmniAVEncoder::ECodec::HEVC : OmniAVEncoder::ECodec::H264;
    EncoderInit.CodecConfig = CodecConfig;
    EncoderInit.Layers.Add(LayerConfig);

    auto OnEncodedPacket = OmniAVEncoder::FVideoEncoder::FOnEncodedPacket::CreateLambda([this](const OmniAVEncoder::FVideoEncoder::FEncodedPacket& Packet)
    {
        FScopeLock Lock(&EncoderCS);
        if (!BitstreamFile)
        {
            return;
        }

        AnnexBBuffer.Reset();
        Packet.ToAnnexB(AnnexBBuffer);
        if (AnnexBBuffer.Num() > 0)
        {
            BitstreamFile->Write(AnnexBBuffer.GetData(), AnnexBBuffer.Num());
        }
    });

    VideoEncoder = OmniAVEncoder::FVideoEncoderFactory::Create(*EncoderInput, EncoderInit, MoveTemp(OnEncodedPacket));
    if (!VideoEncoder.IsValid())
    {
        const FString CodecName = StaticEnum<EOmniCaptureCodec>()->GetNameStringByValue(static_cast<int64>(RequestedCodec));
        LastErrorMessage = FString::Printf(TEXT("Failed to create NVENC video encoder for codec %s."), *CodecName);
        UE_LOG(LogTemp, Error, TEXT("%s"), *LastErrorMessage);
        EncoderInput.Reset();
        return;
    }

    BitstreamFile.Reset(PlatformFile.OpenWrite(*OutputFilePath, /*bAppend=*/false));
    if (!BitstreamFile)
    {
        LastErrorMessage = FString::Printf(TEXT("Unable to open NVENC bitstream output file at %s."), *OutputFilePath);
        UE_LOG(LogTemp, Warning, TEXT("%s"), *LastErrorMessage);
    }

    bInitialized = true;
    UE_LOG(LogTemp, Log, TEXT("NVENC encoder ready (%dx%d, %s, ZeroCopy=%s)."), OutputWidth, OutputHeight, bUseHEVC ? TEXT("HEVC") : TEXT("H.264"), bZeroCopyRequested ? TEXT("Yes") : TEXT("No"));
#else
    LastErrorMessage = TEXT("NVENC is only available on Windows builds with AVEncoder support.");
    UE_LOG(LogTemp, Warning, TEXT("%s"), *LastErrorMessage);
#endif
}

void FOmniCaptureNVENCEncoder::EnqueueFrame(const FOmniCaptureFrame& Frame)
{
#if OMNI_WITH_AVENCODER
    if (!bInitialized || !VideoEncoder.IsValid() || !EncoderInput.IsValid())
    {
        return;
    }

    if (Frame.ReadyFence.IsValid())
    {
        RHIWaitGPUFence(Frame.ReadyFence);
    }

    if (Frame.bUsedCPUFallback)
    {
        UE_LOG(LogTemp, Warning, TEXT("Skipping NVENC submission because frame used CPU equirect fallback."));
        return;
    }

    if (!Frame.Texture.IsValid())
    {
        return;
    }

    TSharedPtr<OmniAVEncoder::FVideoEncoderInputFrame> InputFrame;
    if (Frame.EncoderTextures.Num() > 0)
    {
        InputFrame = EncoderInput->CreateEncoderInputFrame();
        if (InputFrame.IsValid())
        {
            for (int32 PlaneIndex = 0; PlaneIndex < Frame.EncoderTextures.Num(); ++PlaneIndex)
            {
                if (Frame.EncoderTextures[PlaneIndex].IsValid())
                {
                    InputFrame->SetTexture(PlaneIndex, Frame.EncoderTextures[PlaneIndex]);
                }
            }
        }
    }

    if (!InputFrame.IsValid())
    {
        InputFrame = EncoderInput->CreateEncoderInputFrameFromRHITexture(Frame.Texture);
    }

    if (!InputFrame.IsValid())
    {
        return;
    }

    InputFrame->SetTimestampUs(static_cast<uint64>(Frame.Metadata.Timecode * 1'000'000.0));
    InputFrame->SetFrameIndex(Frame.Metadata.FrameIndex);
    InputFrame->SetKeyFrame(Frame.Metadata.bKeyFrame);

    VideoEncoder->Encode(InputFrame);
#else
    (void)Frame;
#endif
}

void FOmniCaptureNVENCEncoder::Finalize()
{
#if OMNI_WITH_AVENCODER
    if (!bInitialized)
    {
        LastErrorMessage.Reset();
        return;
    }

    VideoEncoder.Reset();
    EncoderInput.Reset();

    if (BitstreamFile)
    {
        BitstreamFile->Flush();
        BitstreamFile.Reset();
    }

    UE_LOG(LogTemp, Log, TEXT("NVENC finalize complete -> %s"), *OutputFilePath);
#endif
    bInitialized = false;
    LastErrorMessage.Reset();
}
