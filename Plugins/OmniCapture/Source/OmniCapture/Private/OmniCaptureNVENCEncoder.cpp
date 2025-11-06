#include "OmniCaptureNVENCEncoder.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "OmniCaptureTypes.h"
#include "Math/UnrealMathUtility.h"
#include "PixelFormat.h"
#include "RHI.h"
#include "UObject/UnrealType.h"
#include "Logging/LogMacros.h"
#include "Templates/RefCounting.h"
#if __has_include("RHIAdapter.h")
#include "RHIAdapter.h"
#define OMNI_HAS_RHI_ADAPTER 1
#else
#define OMNI_HAS_RHI_ADAPTER 0
#endif
#include "GenericPlatform/GenericPlatformDriver.h"
#include "Interfaces/IPluginManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogOmniCaptureNVENC, Log, All);

#if OMNI_WITH_NVENC
#include "RHICommandList.h"
#include "RHIResources.h"
#if PLATFORM_WINDOWS && WITH_D3D11_RHI
#include "D3D11RHI.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#include <d3d11.h>
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#if PLATFORM_WINDOWS && WITH_D3D12_RHI
#include "D3D12RHI.h"
#endif
#endif

namespace
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    using namespace OmniNVENC;

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

    FCriticalSection& GetProbeCacheMutex()
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

    FString& GetRuntimeDirectoryOverride()
    {
        static FString OverridePath;
        return OverridePath;
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

    FString ResolveRuntimeDirectoryOverride()
    {
        FString OverridePath = NormalizePath(GetRuntimeDirectoryOverride());
        if (OverridePath.IsEmpty())
        {
            return FString();
        }

        if (FPaths::FileExists(OverridePath))
        {
            OverridePath = FPaths::GetPath(OverridePath);
            FPaths::MakePlatformFilename(OverridePath);
        }
        return OverridePath;
    }

    FString ResolveDllOverridePath()
    {
        FString OverridePath = NormalizePath(GetDllOverridePath());
        if (OverridePath.IsEmpty())
        {
            return FString();
        }

        if (FPaths::DirectoryExists(OverridePath))
        {
#if PLATFORM_64BITS
            OverridePath = FPaths::Combine(OverridePath, TEXT("nvEncodeAPI64.dll"));
#else
            OverridePath = FPaths::Combine(OverridePath, TEXT("nvEncodeAPI.dll"));
#endif
        }
        return OverridePath;
    }

    FString FindBundledRuntimeDirectory()
    {
#if PLATFORM_WINDOWS
        if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OmniCapture")))
        {
            const FString BaseDir = Plugin->GetBaseDir();
            TArray<FString> CandidateDirectories;
            CandidateDirectories.Add(FPaths::Combine(BaseDir, TEXT("Binaries/Win64")));
            CandidateDirectories.Add(FPaths::Combine(BaseDir, TEXT("Binaries")));
            CandidateDirectories.Add(FPaths::Combine(BaseDir, TEXT("Binaries/ThirdParty/Win64")));
            CandidateDirectories.Add(FPaths::Combine(BaseDir, TEXT("ThirdParty/NVENC/Win64")));
            CandidateDirectories.Add(FPaths::Combine(BaseDir, TEXT("ThirdParty/NVENC")));

#if PLATFORM_64BITS
            const FString DllName = TEXT("nvEncodeAPI64.dll");
#else
            const FString DllName = TEXT("nvEncodeAPI.dll");
#endif

            for (const FString& CandidateDirectory : CandidateDirectories)
            {
                if (CandidateDirectory.IsEmpty())
                {
                    continue;
                }

                const FString AbsoluteDirectory = FPaths::ConvertRelativePathToFull(CandidateDirectory);
                const FString CandidateDllPath = FPaths::Combine(AbsoluteDirectory, DllName);
                if (FPaths::FileExists(CandidateDllPath))
                {
                    FString NormalizedDirectory = AbsoluteDirectory;
                    FPaths::NormalizeDirectoryName(NormalizedDirectory);
                    return NormalizedDirectory;
                }
            }
        }
#endif
        return FString();
    }

    void ApplyRuntimeOverrides()
    {
        FString RuntimeDirectory = ResolveRuntimeDirectoryOverride();
        if (RuntimeDirectory.IsEmpty())
        {
            RuntimeDirectory = FindBundledRuntimeDirectory();
        }

        FNVENCCommon::SetSearchDirectory(RuntimeDirectory);

        const FString DllPath = ResolveDllOverridePath();
        FNVENCCommon::SetOverrideDllPath(DllPath);
    }

    ERHIInterfaceType GetCurrentRHIType()
    {
        return GDynamicRHI ? GDynamicRHI->GetInterfaceType() : ERHIInterfaceType::Null;
    }

    bool SupportsEnginePixelFormat(EOmniCaptureColorFormat Format)
    {
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
    }

    ENVENCCodec ToCodec(EOmniCaptureCodec Codec)
    {
        return Codec == EOmniCaptureCodec::HEVC ? ENVENCCodec::HEVC : ENVENCCodec::H264;
    }

    ENVENCBufferFormat ToBufferFormat(EOmniCaptureColorFormat Format)
    {
        switch (Format)
        {
        case EOmniCaptureColorFormat::P010:
            return ENVENCBufferFormat::P010;
        case EOmniCaptureColorFormat::BGRA:
            return ENVENCBufferFormat::BGRA;
        case EOmniCaptureColorFormat::NV12:
        default:
            return ENVENCBufferFormat::NV12;
        }
    }

    ENVENCRateControlMode ToRateControlMode(EOmniCaptureRateControlMode Mode)
    {
        switch (Mode)
        {
        case EOmniCaptureRateControlMode::VariableBitrate:
            return ENVENCRateControlMode::VBR;
        case EOmniCaptureRateControlMode::Lossless:
            return ENVENCRateControlMode::CONSTQP;
        case EOmniCaptureRateControlMode::ConstantBitrate:
        default:
            return ENVENCRateControlMode::CBR;
        }
    }

    bool TryCreateProbeSession(ENVENCCodec Codec, ENVENCBufferFormat Format, FString& OutFailureReason)
    {
#if !PLATFORM_WINDOWS
        OutFailureReason = TEXT("NVENC probe requires Windows.");
        return false;
#else
        ID3D11Device* Device = nullptr;
#if WITH_D3D11_RHI
        uint32 Flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        const D3D_FEATURE_LEVEL FeatureLevels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        TRefCountPtr<ID3D11Device> LocalDevice;
        TRefCountPtr<ID3D11DeviceContext> LocalContext;
        HRESULT Hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, Flags, FeatureLevels, UE_ARRAY_COUNT(FeatureLevels), D3D11_SDK_VERSION, LocalDevice.GetInitReference(), nullptr, LocalContext.GetInitReference());
        if (FAILED(Hr))
        {
            OutFailureReason = FString::Printf(TEXT("Failed to create probing D3D11 device (0x%08x)."), Hr);
            return false;
        }
        Device = LocalDevice;
#else
        OutFailureReason = TEXT("D3D11 support is required for NVENC probing in this build.");
        return false;
#endif

        FNVENCSession Session;
        if (!Session.Open(Codec, Device, NV_ENC_DEVICE_TYPE_DIRECTX))
        {
            OutFailureReason = TEXT("Unable to open NVENC session for probe.");
            return false;
        }

        FNVENCParameters Parameters;
        Parameters.Codec = Codec;
        Parameters.BufferFormat = Format;
        Parameters.Width = 256;
        Parameters.Height = 144;
        Parameters.Framerate = 60;
        Parameters.TargetBitrate = 5 * 1000 * 1000;
        Parameters.MaxBitrate = 10 * 1000 * 1000;
        Parameters.GOPLength = 60;
        Parameters.RateControlMode = ENVENCRateControlMode::CBR;
        Parameters.MultipassMode = ENVENCMultipassMode::DISABLED;

        if (!Session.Initialize(Parameters))
        {
            OutFailureReason = TEXT("Failed to initialise NVENC session during probe.");
            Session.Destroy();
            return false;
        }

        FNVENCBitstream Bitstream;
        if (!Bitstream.Initialize(Session.GetEncoderHandle(), Session.GetFunctionList()))
        {
            OutFailureReason = TEXT("Failed to allocate NVENC bitstream during probe.");
            Session.Destroy();
            return false;
        }

        Session.Destroy();
        return true;
#endif
    }

    FNVENCHardwareProbeResult RunNVENCHardwareProbe()
    {
        ApplyRuntimeOverrides();

        FNVENCHardwareProbeResult Result;

        if (!FNVENCCommon::EnsureLoaded())
        {
            Result.DllFailureReason = TEXT("Unable to load nvEncodeAPI runtime.");
            return Result;
        }

        Result.bDllPresent = true;

        FNVEncodeAPILoader& Loader = FNVEncodeAPILoader::Get();
        if (!Loader.Load())
        {
            Result.ApiFailureReason = TEXT("Failed to resolve NVENC exports.");
            return Result;
        }

        Result.bApisReady = true;

        FString SessionFailure;
        if (!TryCreateProbeSession(ENVENCCodec::H264, ENVENCBufferFormat::NV12, SessionFailure))
        {
            Result.SessionFailureReason = SessionFailure;
            return Result;
        }

        Result.bSessionOpenable = true;
        Result.bSupportsH264 = true;
        Result.bSupportsNV12 = true;

        FString Nv12Failure;
        if (TryCreateProbeSession(ENVENCCodec::H264, ENVENCBufferFormat::BGRA, Nv12Failure))
        {
            Result.bSupportsBGRA = true;
        }
        else
        {
            Result.BGRAFailureReason = Nv12Failure;
        }

        FString HevcFailure;
        if (TryCreateProbeSession(ENVENCCodec::HEVC, ENVENCBufferFormat::NV12, HevcFailure))
        {
            Result.bSupportsHEVC = true;
        }
        else
        {
            Result.CodecFailureReason = HevcFailure;
        }

        FString P010Failure;
        if (TryCreateProbeSession(ENVENCCodec::HEVC, ENVENCBufferFormat::P010, P010Failure))
        {
            Result.bSupportsP010 = true;
        }
        else
        {
            Result.P010FailureReason = P010Failure;
        }

        return Result;
    }
#endif
}

FOmniCaptureNVENCEncoder::FOmniCaptureNVENCEncoder() = default;
FOmniCaptureNVENCEncoder::~FOmniCaptureNVENCEncoder()
{
    Finalize();
}

bool FOmniCaptureNVENCEncoder::IsNVENCAvailable()
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    FScopeLock Lock(&GetProbeCacheMutex());
    if (!GetProbeValidFlag())
    {
        GetCachedProbe() = RunNVENCHardwareProbe();
        GetProbeValidFlag() = true;
    }
    const FNVENCHardwareProbeResult& Probe = GetCachedProbe();
    return Probe.bDllPresent && Probe.bApisReady && Probe.bSessionOpenable;
#else
    return false;
#endif
}

FOmniNVENCCapabilities FOmniCaptureNVENCEncoder::QueryCapabilities()
{
    FOmniNVENCCapabilities Caps;
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    FScopeLock Lock(&GetProbeCacheMutex());
    if (!GetProbeValidFlag())
    {
        GetCachedProbe() = RunNVENCHardwareProbe();
        GetProbeValidFlag() = true;
    }

    const FNVENCHardwareProbeResult& Probe = GetCachedProbe();
    Caps.bDllPresent = Probe.bDllPresent;
    Caps.bApisReady = Probe.bApisReady;
    Caps.bSessionOpenable = Probe.bSessionOpenable;
    Caps.bSupportsHEVC = Probe.bSupportsHEVC;
    Caps.bSupportsNV12 = Probe.bSupportsNV12 && SupportsEnginePixelFormat(EOmniCaptureColorFormat::NV12);
    Caps.bSupportsP010 = Probe.bSupportsP010 && SupportsEnginePixelFormat(EOmniCaptureColorFormat::P010);
    Caps.bSupportsBGRA = Probe.bSupportsBGRA && SupportsEnginePixelFormat(EOmniCaptureColorFormat::BGRA);
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
#else
    Caps.bHardwareAvailable = false;
    Caps.DllFailureReason = TEXT("NVENC is only available on Windows builds.");
    Caps.HardwareFailureReason = Caps.DllFailureReason;
#endif

    Caps.AdapterName = FPlatformMisc::GetPrimaryGPUBrand();
#if PLATFORM_WINDOWS
    FString DeviceDescription;
#if OMNI_HAS_RHI_ADAPTER
    if (GDynamicRHI)
    {
        FRHIAdapterInfo AdapterInfo;
        GDynamicRHI->RHIGetAdapterInfo(AdapterInfo);
        DeviceDescription = AdapterInfo.Description;
    }
#endif
    if (DeviceDescription.IsEmpty())
    {
        DeviceDescription = Caps.AdapterName;
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

bool FOmniCaptureNVENCEncoder::SupportsColorFormat(EOmniCaptureColorFormat Format)
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    return SupportsEnginePixelFormat(Format);
#else
    return false;
#endif
}

bool FOmniCaptureNVENCEncoder::SupportsZeroCopyRHI()
{
#if PLATFORM_WINDOWS
    if (!GDynamicRHI)
    {
        return false;
    }

    const ERHIInterfaceType InterfaceType = GDynamicRHI->GetInterfaceType();
#if WITH_D3D11_RHI
    if (InterfaceType == ERHIInterfaceType::D3D11)
    {
        return true;
    }
#endif
#if WITH_D3D12_RHI
    if (InterfaceType == ERHIInterfaceType::D3D12)
    {
        return true;
    }
#endif
    return false;
#else
    return false;
#endif
}

void FOmniCaptureNVENCEncoder::SetRuntimeDirectoryOverride(const FString& InOverridePath)
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    GetRuntimeDirectoryOverride() = InOverridePath;
    InvalidateCachedCapabilities();
#else
    (void)InOverridePath;
#endif
}

void FOmniCaptureNVENCEncoder::SetDllOverridePath(const FString& InOverridePath)
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    GetDllOverridePath() = InOverridePath;
    InvalidateCachedCapabilities();
#else
    (void)InOverridePath;
#endif
}

void FOmniCaptureNVENCEncoder::InvalidateCachedCapabilities()
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    FScopeLock Lock(&GetProbeCacheMutex());
    GetProbeValidFlag() = false;
#endif
}

void FOmniCaptureNVENCEncoder::Initialize(const FOmniCaptureSettings& Settings, const FString& OutputDirectory)
{
    Finalize();

    LastErrorMessage.Reset();
    bInitialized = false;

#if OMNI_WITH_NVENC
    AnnexB.Reset();
    bAnnexBHeaderWritten = false;
#endif

    const FString FileName = FString::Printf(TEXT("%s.%s"), *Settings.OutputFileName, Settings.Codec == EOmniCaptureCodec::HEVC ? TEXT("h265") : TEXT("h264"));
    OutputFilePath = FPaths::Combine(OutputDirectory, FileName);

    ColorFormat = Settings.NVENCColorFormat;
    RequestedCodec = Settings.Codec;
    bZeroCopyRequested = Settings.bZeroCopy;

#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    ApplyRuntimeOverrides();

    if (!IsNVENCAvailable())
    {
        LastErrorMessage = TEXT("NVENC runtime is unavailable. Falling back to image sequence.");
        UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("%s"), *LastErrorMessage);
        return;
    }

    if (!SupportsEnginePixelFormat(ColorFormat))
    {
        LastErrorMessage = TEXT("Requested NVENC pixel format is not supported by the engine or GPU.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        return;
    }

    if (bZeroCopyRequested && !SupportsZeroCopyRHI())
    {
        UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("Zero-copy NVENC capture requested but RHI does not support it. Falling back to auto copy."));
        bZeroCopyRequested = false;
    }

    const FIntPoint OutputSize = Settings.GetOutputResolution();
    ActiveParameters = FNVENCParameters();
    ActiveParameters.Codec = ToCodec(RequestedCodec);
    ActiveParameters.BufferFormat = ToBufferFormat(ColorFormat);
    ActiveParameters.Width = OutputSize.X;
    ActiveParameters.Height = OutputSize.Y;
    ActiveParameters.Framerate = FMath::Clamp<int32>(FMath::RoundToInt(Settings.TargetFrameRate), 1, 120);
    ActiveParameters.TargetBitrate = Settings.Quality.TargetBitrateKbps * 1000;
    ActiveParameters.MaxBitrate = FMath::Max(Settings.Quality.MaxBitrateKbps * 1000, ActiveParameters.TargetBitrate);
    ActiveParameters.RateControlMode = ToRateControlMode(Settings.Quality.RateControlMode);
    ActiveParameters.MultipassMode = Settings.Quality.bLowLatency ? ENVENCMultipassMode::DISABLED : ENVENCMultipassMode::FULL;
    ActiveParameters.GOPLength = Settings.Quality.GOPLength;
    ActiveParameters.bEnableAdaptiveQuantization = Settings.Quality.RateControlMode != EOmniCaptureRateControlMode::Lossless;
    ActiveParameters.bEnableLookahead = !Settings.Quality.bLowLatency;
    if (Settings.Quality.RateControlMode == EOmniCaptureRateControlMode::Lossless)
    {
        ActiveParameters.QPMin = 0;
        ActiveParameters.QPMax = 0;
    }
    else
    {
        ActiveParameters.QPMin = 0;
        ActiveParameters.QPMax = 51;
    }

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    BitstreamFile.Reset(PlatformFile.OpenWrite(*OutputFilePath, /*bAppend=*/false));
    if (!BitstreamFile)
    {
        LastErrorMessage = FString::Printf(TEXT("Unable to open NVENC output file at %s."), *OutputFilePath);
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        return;
    }

    bInitialized = true;
    UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC encoder primed – waiting for first frame to initialise session (%dx%d, %s)."),
        ActiveParameters.Width,
        ActiveParameters.Height,
        RequestedCodec == EOmniCaptureCodec::HEVC ? TEXT("HEVC") : TEXT("H.264"));
#else
    (void)Settings;
    (void)OutputDirectory;
    LastErrorMessage = TEXT("NVENC is only available on Windows builds.");
    UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("%s"), *LastErrorMessage);
#endif
}

#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
namespace
{
    ID3D11Texture2D* GetD3D11TextureFromRHI(const FTextureRHIRef& Texture)
    {
        return Texture.IsValid() ? static_cast<ID3D11Texture2D*>(Texture->GetNativeResource()) : nullptr;
    }

#if WITH_D3D12_RHI
    ID3D12Resource* GetD3D12ResourceFromRHI(const FTextureRHIRef& Texture)
    {
        return Texture.IsValid() ? static_cast<ID3D12Resource*>(Texture->GetNativeResource()) : nullptr;
    }
#endif

#if WITH_D3D11_RHI
    bool EncodeFrameD3D11(FOmniCaptureNVENCEncoder& Encoder, const FOmniCaptureFrame& Frame)
    {
        ID3D11Texture2D* Texture = GetD3D11TextureFromRHI(Frame.Texture);
        if (!Texture)
        {
            Encoder.LastErrorMessage = TEXT("D3D11 texture was unavailable for NVENC capture.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            return false;
        }

        if (!Encoder.EncoderSession.IsOpen())
        {
            TRefCountPtr<ID3D11Device> Device;
            Texture->GetDevice(Device.GetInitReference());

            if (!Device.IsValid())
            {
                Encoder.LastErrorMessage = TEXT("Unable to retrieve D3D11 device from capture texture.");
                UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
                return false;
            }

            if (!Encoder.EncoderSession.Open(Encoder.ActiveParameters.Codec, Device.GetReference(), NV_ENC_DEVICE_TYPE_DIRECTX))
            {
                Encoder.LastErrorMessage = TEXT("Failed to open NVENC session.");
                UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
                return false;
            }

            if (!Encoder.EncoderSession.Initialize(Encoder.ActiveParameters))
            {
                Encoder.LastErrorMessage = TEXT("Failed to initialise NVENC session.");
                UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
                return false;
            }

            if (!Encoder.Bitstream.Initialize(Encoder.EncoderSession.GetEncoderHandle(), Encoder.EncoderSession.GetFunctionList()))
            {
                Encoder.LastErrorMessage = TEXT("Failed to create NVENC bitstream buffer.");
                UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
                return false;
            }

            if (!Encoder.D3D11Input.Initialise(Device.GetReference(), Encoder.EncoderSession))
            {
                Encoder.LastErrorMessage = TEXT("Failed to initialise NVENC D3D11 input bridge.");
                UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
                return false;
            }

            if (!Encoder.WriteAnnexBHeader())
            {
                UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("NVENC did not supply Annex B headers prior to first frame."));
            }

            UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC session initialised (%dx%d)."), Encoder.ActiveParameters.Width, Encoder.ActiveParameters.Height);
        }
        else if (!Encoder.bAnnexBHeaderWritten)
        {
            Encoder.WriteAnnexBHeader();
        }

        if (!Encoder.D3D11Input.RegisterResource(Texture))
        {
            Encoder.LastErrorMessage = TEXT("Failed to register input texture with NVENC.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            return false;
        }

        NV_ENC_INPUT_PTR MappedInput = nullptr;
        if (!Encoder.D3D11Input.MapResource(Texture, MappedInput) || !MappedInput)
        {
            Encoder.LastErrorMessage = TEXT("Failed to map input texture for NVENC encoding.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            return false;
        }

        NV_ENC_PIC_PARAMS PicParams = {};
        PicParams.version = NV_ENC_PIC_PARAMS_VER;
        PicParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        PicParams.inputBuffer = MappedInput;
        PicParams.bufferFmt = Encoder.EncoderSession.GetNVBufferFormat();
        PicParams.inputWidth = Encoder.ActiveParameters.Width;
        PicParams.inputHeight = Encoder.ActiveParameters.Height;
        PicParams.outputBitstream = Encoder.Bitstream.GetBitstreamBuffer();
        PicParams.inputTimeStamp = static_cast<uint64>(Frame.Metadata.Timecode * 1'000'000.0);
        PicParams.frameIdx = Frame.Metadata.FrameIndex;
        if (Frame.Metadata.bKeyFrame)
        {
            PicParams.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEINTRA;
        }

        auto EncodePicture = Encoder.EncoderSession.GetFunctionList().nvEncEncodePicture;
        if (!EncodePicture)
        {
            Encoder.LastErrorMessage = TEXT("NVENC function table missing nvEncEncodePicture.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            Encoder.D3D11Input.UnmapResource(MappedInput);
            return false;
        }

        NVENCSTATUS Status = EncodePicture(Encoder.EncoderSession.GetEncoderHandle(), &PicParams);
        if (Status != NV_ENC_SUCCESS)
        {
            Encoder.LastErrorMessage = FString::Printf(TEXT("nvEncEncodePicture failed: %s"), *FNVENCDefs::StatusToString(Status));
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            Encoder.D3D11Input.UnmapResource(MappedInput);
            return false;
        }

        void* BitstreamData = nullptr;
        int32 BitstreamSize = 0;
        if (!Encoder.Bitstream.Lock(BitstreamData, BitstreamSize))
        {
            Encoder.LastErrorMessage = TEXT("Failed to lock NVENC bitstream.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            Encoder.D3D11Input.UnmapResource(MappedInput);
            return false;
        }

        OmniNVENC::FNVENCEncodedPacket Packet;
        if (Encoder.Bitstream.ExtractPacket(Packet) && Packet.Data.Num() > 0 && Encoder.BitstreamFile)
        {
            Encoder.BitstreamFile->Write(Packet.Data.GetData(), Packet.Data.Num());
        }

        Encoder.Bitstream.Unlock();
        Encoder.D3D11Input.UnmapResource(MappedInput);
        return true;
    }
#endif

#if WITH_D3D12_RHI
    bool EncodeFrameD3D12(FOmniCaptureNVENCEncoder& Encoder, const FOmniCaptureFrame& Frame)
    {
        ID3D12Resource* Resource = GetD3D12ResourceFromRHI(Frame.Texture);
        if (!Resource)
        {
            Encoder.LastErrorMessage = TEXT("D3D12 resource was unavailable for NVENC capture.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            return false;
        }

        TRefCountPtr<ID3D12Device> Device12;
        HRESULT DeviceResult = Resource->GetDevice(IID_PPV_ARGS(Device12.GetInitReference()));
        if (FAILED(DeviceResult) || !Device12.IsValid())
        {
            Encoder.LastErrorMessage = TEXT("Unable to retrieve D3D12 device from capture texture.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s (0x%08x)."), *Encoder.LastErrorMessage, DeviceResult);
            return false;
        }

        if (!Encoder.D3D12Input.IsInitialised())
        {
            if (!Encoder.D3D12Input.Initialise(Device12.GetReference()))
            {
                Encoder.LastErrorMessage = TEXT("Failed to initialise NVENC D3D12 bridge.");
                UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
                return false;
            }
        }

        ID3D11Device* BridgeDevice = Encoder.D3D12Input.GetD3D11Device();
        if (!BridgeDevice)
        {
            Encoder.LastErrorMessage = TEXT("D3D11-on-12 bridge device is unavailable for NVENC capture.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            return false;
        }

        if (!Encoder.EncoderSession.IsOpen())
        {
            if (!Encoder.EncoderSession.Open(Encoder.ActiveParameters.Codec, BridgeDevice, NV_ENC_DEVICE_TYPE_DIRECTX))
            {
                Encoder.LastErrorMessage = TEXT("Failed to open NVENC session through D3D12 bridge.");
                UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
                return false;
            }

            if (!Encoder.EncoderSession.Initialize(Encoder.ActiveParameters))
            {
                Encoder.LastErrorMessage = TEXT("Failed to initialise NVENC session.");
                UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
                return false;
            }

            if (!Encoder.Bitstream.Initialize(Encoder.EncoderSession.GetEncoderHandle(), Encoder.EncoderSession.GetFunctionList()))
            {
                Encoder.LastErrorMessage = TEXT("Failed to create NVENC bitstream buffer.");
                UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
                return false;
            }

            if (!Encoder.D3D12Input.BindSession(Encoder.EncoderSession))
            {
                Encoder.LastErrorMessage = TEXT("Failed to bind NVENC session to D3D12 bridge.");
                UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
                return false;
            }

            if (!Encoder.WriteAnnexBHeader())
            {
                UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("NVENC did not provide Annex B headers before first D3D12 frame."));
            }

            UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC session initialised via D3D12 bridge (%dx%d)."), Encoder.ActiveParameters.Width, Encoder.ActiveParameters.Height);
        }
        else
        {
            if (!Encoder.D3D12Input.IsSessionBound() && !Encoder.D3D12Input.BindSession(Encoder.EncoderSession))
            {
                Encoder.LastErrorMessage = TEXT("Failed to rebind NVENC session to D3D12 bridge.");
                UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
                return false;
            }

            if (!Encoder.bAnnexBHeaderWritten)
            {
                Encoder.WriteAnnexBHeader();
            }
        }

        if (!Encoder.D3D12Input.RegisterResource(Resource))
        {
            Encoder.LastErrorMessage = TEXT("Failed to register D3D12 resource with NVENC.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            return false;
        }

        NV_ENC_INPUT_PTR MappedInput = nullptr;
        if (!Encoder.D3D12Input.MapResource(Resource, MappedInput) || !MappedInput)
        {
            Encoder.LastErrorMessage = TEXT("Failed to map D3D12 resource for NVENC encoding.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            return false;
        }

        NV_ENC_PIC_PARAMS PicParams = {};
        PicParams.version = NV_ENC_PIC_PARAMS_VER;
        PicParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        PicParams.inputBuffer = MappedInput;
        PicParams.bufferFmt = Encoder.EncoderSession.GetNVBufferFormat();
        PicParams.inputWidth = Encoder.ActiveParameters.Width;
        PicParams.inputHeight = Encoder.ActiveParameters.Height;
        PicParams.outputBitstream = Encoder.Bitstream.GetBitstreamBuffer();
        PicParams.inputTimeStamp = static_cast<uint64>(Frame.Metadata.Timecode * 1'000'000.0);
        PicParams.frameIdx = Frame.Metadata.FrameIndex;
        if (Frame.Metadata.bKeyFrame)
        {
            PicParams.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEINTRA;
        }

        auto EncodePicture = Encoder.EncoderSession.GetFunctionList().nvEncEncodePicture;
        if (!EncodePicture)
        {
            Encoder.LastErrorMessage = TEXT("NVENC function table missing nvEncEncodePicture.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            Encoder.D3D12Input.UnmapResource(MappedInput);
            return false;
        }

        NVENCSTATUS Status = EncodePicture(Encoder.EncoderSession.GetEncoderHandle(), &PicParams);
        if (Status != NV_ENC_SUCCESS)
        {
            Encoder.LastErrorMessage = FString::Printf(TEXT("nvEncEncodePicture failed: %s"), *FNVENCDefs::StatusToString(Status));
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            Encoder.D3D12Input.UnmapResource(MappedInput);
            return false;
        }

        void* BitstreamData = nullptr;
        int32 BitstreamSize = 0;
        if (!Encoder.Bitstream.Lock(BitstreamData, BitstreamSize))
        {
            Encoder.LastErrorMessage = TEXT("Failed to lock NVENC bitstream.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *Encoder.LastErrorMessage);
            Encoder.D3D12Input.UnmapResource(MappedInput);
            return false;
        }

        OmniNVENC::FNVENCEncodedPacket Packet;
        if (Encoder.Bitstream.ExtractPacket(Packet) && Packet.Data.Num() > 0 && Encoder.BitstreamFile)
        {
            Encoder.BitstreamFile->Write(Packet.Data.GetData(), Packet.Data.Num());
        }

        Encoder.Bitstream.Unlock();
        Encoder.D3D12Input.UnmapResource(MappedInput);
        return true;
    }
#endif

    bool EncodeFrameInternal(FOmniCaptureNVENCEncoder& Encoder, const FOmniCaptureFrame& Frame)
    {
        if (!GDynamicRHI)
        {
            return false;
        }

        const ERHIInterfaceType InterfaceType = GDynamicRHI->GetInterfaceType();

#if WITH_D3D11_RHI
        if (InterfaceType == ERHIInterfaceType::D3D11)
        {
            return EncodeFrameD3D11(Encoder, Frame);
        }
#endif

#if WITH_D3D12_RHI
        if (InterfaceType == ERHIInterfaceType::D3D12)
        {
            return EncodeFrameD3D12(Encoder, Frame);
        }
#endif

        UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("NVENC capture is not implemented for RHI interface %d."), static_cast<int32>(InterfaceType));
        return false;
    }
}
#endif

void FOmniCaptureNVENCEncoder::EnqueueFrame(const FOmniCaptureFrame& Frame)
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    if (!bInitialized || !BitstreamFile)
    {
        return;
    }

    if (Frame.ReadyFence.IsValid())
    {
        RHIWaitGPUFence(Frame.ReadyFence);
    }

    if (Frame.bUsedCPUFallback)
    {
        UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("Skipping NVENC submission because frame used CPU fallback."));
        return;
    }

    if (!Frame.Texture.IsValid())
    {
        return;
    }

    FScopeLock Lock(&EncoderCS);
    EncodeFrameInternal(*this, Frame);
#else
    (void)Frame;
#endif
}

void FOmniCaptureNVENCEncoder::Finalize()
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    FScopeLock Lock(&EncoderCS);

    if (BitstreamFile)
    {
        BitstreamFile->Flush();
        BitstreamFile.Reset();
    }

    Bitstream.Release();
    D3D11Input.Shutdown();
    D3D12Input.Shutdown();
    EncoderSession.Flush();
    EncoderSession.Destroy();
    AnnexB.Reset();
    bAnnexBHeaderWritten = false;
#endif

    bInitialized = false;
    LastErrorMessage.Reset();
}

#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
bool FOmniCaptureNVENCEncoder::WriteAnnexBHeader()
{
    if (bAnnexBHeaderWritten || !EncoderSession.IsInitialised())
    {
        return bAnnexBHeaderWritten;
    }

    TArray<uint8> SequenceData;
    if (!EncoderSession.GetSequenceParams(SequenceData) || SequenceData.Num() == 0)
    {
        return false;
    }

    AnnexB.SetCodecConfig(SequenceData);
    const TArray<uint8>& Header = AnnexB.GetCodecConfig();
    if (Header.Num() == 0 || !BitstreamFile)
    {
        return false;
    }

    BitstreamFile->Write(Header.GetData(), Header.Num());
    bAnnexBHeaderWritten = true;
    UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("Wrote NVENC Annex B header (%d bytes)."), Header.Num());
    return true;
}
#else
bool FOmniCaptureNVENCEncoder::WriteAnnexBHeader()
{
    return false;
}
#endif

