// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCSession.h"

#include "NVENC/NVENCCommon.h"
#include "NVENC/NVENCDefs.h"
#include "NVENC/NVENCParameters.h"
#include "NVENC/NVEncodeAPILoader.h"
#include "Logging/LogMacros.h"

#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCSession, Log, All);

namespace OmniNVENC
{
    namespace
    {
#if PLATFORM_WINDOWS
        GUID ToWindowsGuid(const FGuid& InGuid)
        {
            GUID Guid;
            Guid.Data1 = static_cast<uint32>(InGuid.A);
            Guid.Data2 = static_cast<uint16>((static_cast<uint32>(InGuid.B) >> 16) & 0xFFFF);
            Guid.Data3 = static_cast<uint16>(static_cast<uint32>(InGuid.B) & 0xFFFF);

            const uint32 C = static_cast<uint32>(InGuid.C);
            const uint32 D = static_cast<uint32>(InGuid.D);

            Guid.Data4[0] = static_cast<uint8>((C >> 24) & 0xFF);
            Guid.Data4[1] = static_cast<uint8>((C >> 16) & 0xFF);
            Guid.Data4[2] = static_cast<uint8>((C >> 8) & 0xFF);
            Guid.Data4[3] = static_cast<uint8>(C & 0xFF);
            Guid.Data4[4] = static_cast<uint8>((D >> 24) & 0xFF);
            Guid.Data4[5] = static_cast<uint8>((D >> 16) & 0xFF);
            Guid.Data4[6] = static_cast<uint8>((D >> 8) & 0xFF);
            Guid.Data4[7] = static_cast<uint8>(D & 0xFF);
            return Guid;
        }

        NV_ENC_BUFFER_FORMAT ToNVFormat(ENVENCBufferFormat Format)
        {
            switch (Format)
            {
            case ENVENCBufferFormat::P010:
                return NV_ENC_BUFFER_FORMAT_YUV420_10BIT;
            case ENVENCBufferFormat::BGRA:
                return NV_ENC_BUFFER_FORMAT_ARGB;
            case ENVENCBufferFormat::NV12:
            default:
                return NV_ENC_BUFFER_FORMAT_NV12;
            }
        }

        NV_ENC_PARAMS_RC_MODE ToNVRateControl(ENVENCRateControlMode Mode)
        {
            switch (Mode)
            {
            case ENVENCRateControlMode::CONSTQP:
                return NV_ENC_PARAMS_RC_CONSTQP;
            case ENVENCRateControlMode::VBR:
                return NV_ENC_PARAMS_RC_VBR;
            case ENVENCRateControlMode::CBR:
            default:
                return NV_ENC_PARAMS_RC_CBR;
            }
        }

        NV_ENC_MULTI_PASS ToNVMultiPass(ENVENCMultipassMode Mode)
        {
            switch (Mode)
            {
            case ENVENCMultipassMode::QUARTER:
                return NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
            case ENVENCMultipassMode::FULL:
                return NV_ENC_TWO_PASS_FULL_RESOLUTION;
            case ENVENCMultipassMode::DISABLED:
            default:
                return NV_ENC_MULTI_PASS_DISABLED;
            }
        }

        template <typename TFunc>
        bool ValidateFunction(const ANSICHAR* Name, TFunc* Function)
        {
            if (!Function)
            {
                UE_LOG(LogNVENCSession, Error, TEXT("Required NVENC export '%s' is missing."), ANSI_TO_TCHAR(Name));
                return false;
            }
            return true;
        }
#endif
    }

    bool FNVENCSession::Open(ENVENCCodec Codec, void* InDevice, NV_ENC_DEVICE_TYPE InDeviceType)
    {
#if !PLATFORM_WINDOWS
        UE_LOG(LogNVENCSession, Warning, TEXT("NVENC session is only available on Windows builds."));
        return false;
#else
        if (bIsOpen)
        {
            return true;
        }

        if (!InDevice)
        {
            UE_LOG(LogNVENCSession, Error, TEXT("Failed to open NVENC session – no encoder device was provided."));
            return false;
        }

        FNVEncodeAPILoader& Loader = FNVEncodeAPILoader::Get();
        if (!Loader.Load())
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("Failed to open NVENC session for codec %s – runtime is unavailable."), *FNVENCDefs::CodecToString(Codec));
            return false;
        }

        using TNvEncodeAPICreateInstance = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);
        using TNvEncOpenEncodeSessionEx = NVENCSTATUS(NVENCAPI*)(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS*, void**);

        TNvEncodeAPICreateInstance CreateInstance = reinterpret_cast<TNvEncodeAPICreateInstance>(Loader.GetFunctions().NvEncodeAPICreateInstance);
        TNvEncOpenEncodeSessionEx OpenSession = reinterpret_cast<TNvEncOpenEncodeSessionEx>(Loader.GetFunctions().NvEncOpenEncodeSessionEx);

        if (!ValidateFunction("NvEncodeAPICreateInstance", CreateInstance) || !ValidateFunction("NvEncOpenEncodeSessionEx", OpenSession))
        {
            return false;
        }

        FMemory::Memzero(FunctionList);
        FunctionList.version = NV_ENCODE_API_FUNCTION_LIST_VER;

        NVENCSTATUS Status = CreateInstance(&FunctionList);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCSession, Error, TEXT("NvEncodeAPICreateInstance failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS OpenParams = {};
        OpenParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        OpenParams.apiVersion = NVENCAPI_VERSION;
        OpenParams.device = InDevice;
        OpenParams.deviceType = InDeviceType;

        Status = OpenSession(&OpenParams, &Encoder);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCSession, Error, TEXT("NvEncOpenEncodeSessionEx failed: %s"), *FNVENCDefs::StatusToString(Status));
            Encoder = nullptr;
            return false;
        }

        Device = InDevice;
        DeviceType = InDeviceType;
        CurrentParameters.Codec = Codec;
        bIsOpen = true;
        return true;
#endif
    }

    bool FNVENCSession::Initialize(const FNVENCParameters& Parameters)
    {
#if !PLATFORM_WINDOWS
        UE_LOG(LogNVENCSession, Warning, TEXT("Cannot initialise NVENC session on this platform."));
        return false;
#else
        if (!bIsOpen || !Encoder)
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("Cannot initialise NVENC session – encoder is not open."));
            return false;
        }

        using TNvEncGetEncodePresetConfig = NVENCSTATUS(NVENCAPI*)(void*, GUID, GUID, NV_ENC_PRESET_CONFIG*);
        using TNvEncInitializeEncoder = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_INITIALIZE_PARAMS*);

        TNvEncGetEncodePresetConfig GetPresetConfig = FunctionList.nvEncGetEncodePresetConfig;
        TNvEncInitializeEncoder InitializeEncoder = FunctionList.nvEncInitializeEncoder;

        if (!ValidateFunction("NvEncGetEncodePresetConfig", GetPresetConfig) || !ValidateFunction("NvEncInitializeEncoder", InitializeEncoder))
        {
            return false;
        }

        GUID CodecGuid = ToWindowsGuid(FNVENCDefs::CodecGuid(Parameters.Codec));
        GUID PresetGuid = ToWindowsGuid(FNVENCDefs::PresetLowLatencyGuid());

        NV_ENC_PRESET_CONFIG PresetConfig = {};
        PresetConfig.version = NV_ENC_PRESET_CONFIG_VER;
        PresetConfig.presetCfg.version = NV_ENC_CONFIG_VER;

        NVENCSTATUS Status = GetPresetConfig(Encoder, CodecGuid, PresetGuid, &PresetConfig);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCSession, Error, TEXT("NvEncGetEncodePresetConfig failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        EncodeConfig = PresetConfig.presetCfg;
        EncodeConfig.rcParams.rateControlMode = ToNVRateControl(Parameters.RateControlMode);
        EncodeConfig.rcParams.averageBitRate = Parameters.TargetBitrate;
        EncodeConfig.rcParams.maxBitRate = Parameters.MaxBitrate;
        EncodeConfig.rcParams.enableLookahead = Parameters.bEnableLookahead ? 1u : 0u;
        EncodeConfig.rcParams.enableAQ = Parameters.bEnableAdaptiveQuantization ? 1u : 0u;
        EncodeConfig.rcParams.enableTemporalAQ = Parameters.bEnableAdaptiveQuantization ? 1u : 0u;
        EncodeConfig.rcParams.enableInitialRCQP = (Parameters.QPMax >= 0 || Parameters.QPMin >= 0) ? 1u : 0u;
        EncodeConfig.rcParams.constQP.qpInterB = Parameters.QPMax >= 0 ? Parameters.QPMax : EncodeConfig.rcParams.constQP.qpInterB;
        EncodeConfig.rcParams.constQP.qpInterP = Parameters.QPMax >= 0 ? Parameters.QPMax : EncodeConfig.rcParams.constQP.qpInterP;
        EncodeConfig.rcParams.constQP.qpIntra = Parameters.QPMin >= 0 ? Parameters.QPMin : EncodeConfig.rcParams.constQP.qpIntra;
        EncodeConfig.rcParams.multiPass = ToNVMultiPass(Parameters.MultipassMode);
        EncodeConfig.gopLength = Parameters.GOPLength == 0 ? NVENC_INFINITE_GOPLENGTH : Parameters.GOPLength;
        EncodeConfig.frameIntervalP = 1;
        EncodeConfig.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
        EncodeConfig.mvPrecision = NV_ENC_MV_PRECISION_QUARTER_PEL;

        if (Parameters.Codec == ENVENCCodec::H264)
        {
            EncodeConfig.profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
            EncodeConfig.encodeCodecConfig.h264Config.idrPeriod = EncodeConfig.gopLength;
        }
        else
        {
            EncodeConfig.profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;
            EncodeConfig.encodeCodecConfig.hevcConfig.idrPeriod = EncodeConfig.gopLength;
        }

        NvBufferFormat = ToNVFormat(Parameters.BufferFormat);

        InitializeParams = {};
        InitializeParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
        InitializeParams.encodeGUID = CodecGuid;
        InitializeParams.presetGUID = PresetGuid;
        InitializeParams.tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY;
        InitializeParams.encodeWidth = Parameters.Width;
        InitializeParams.encodeHeight = Parameters.Height;
        InitializeParams.darWidth = Parameters.Width;
        InitializeParams.darHeight = Parameters.Height;
        InitializeParams.frameRateNum = Parameters.Framerate == 0 ? 60 : Parameters.Framerate;
        InitializeParams.frameRateDen = 1;
        InitializeParams.enablePTD = 1;
        InitializeParams.encodeConfig = &EncodeConfig;
        InitializeParams.maxEncodeWidth = Parameters.Width;
        InitializeParams.maxEncodeHeight = Parameters.Height;
        InitializeParams.bufferFormat = NvBufferFormat;
        InitializeParams.enableEncodeAsync = 0;

        Status = InitializeEncoder(Encoder, &InitializeParams);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCSession, Error, TEXT("NvEncInitializeEncoder failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        CurrentParameters = Parameters;
        bIsInitialised = true;
        UE_LOG(LogNVENCSession, Verbose, TEXT("NVENC session initialised: %s"), *FNVENCParameterMapper::ToDebugString(CurrentParameters));
        return true;
#endif
    }

    bool FNVENCSession::Reconfigure(const FNVENCParameters& Parameters)
    {
#if !PLATFORM_WINDOWS
        return false;
#else
        if (!bIsInitialised)
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("Cannot reconfigure NVENC session – encoder has not been initialised."));
            return false;
        }

        using TNvEncReconfigureEncoder = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_RECONFIGURE_PARAMS*);

        TNvEncReconfigureEncoder ReconfigureEncoder = FunctionList.nvEncReconfigureEncoder;
        if (!ValidateFunction("NvEncReconfigureEncoder", ReconfigureEncoder))
        {
            return false;
        }

        NV_ENC_CONFIG NewConfig = EncodeConfig;
        NewConfig.rcParams.rateControlMode = ToNVRateControl(Parameters.RateControlMode);
        NewConfig.rcParams.averageBitRate = Parameters.TargetBitrate;
        NewConfig.rcParams.maxBitRate = Parameters.MaxBitrate;
        NewConfig.rcParams.enableLookahead = Parameters.bEnableLookahead ? 1u : 0u;
        NewConfig.rcParams.enableAQ = Parameters.bEnableAdaptiveQuantization ? 1u : 0u;
        NewConfig.rcParams.enableTemporalAQ = Parameters.bEnableAdaptiveQuantization ? 1u : 0u;
        NewConfig.rcParams.multiPass = ToNVMultiPass(Parameters.MultipassMode);
        NewConfig.gopLength = Parameters.GOPLength == 0 ? NVENC_INFINITE_GOPLENGTH : Parameters.GOPLength;

        NV_ENC_RECONFIGURE_PARAMS ReconfigureParams = {};
        ReconfigureParams.version = NV_ENC_RECONFIGURE_PARAMS_VER;
        ReconfigureParams.reInitEncodeParams = InitializeParams;
        ReconfigureParams.reInitEncodeParams.encodeWidth = Parameters.Width;
        ReconfigureParams.reInitEncodeParams.encodeHeight = Parameters.Height;
        ReconfigureParams.reInitEncodeParams.darWidth = Parameters.Width;
        ReconfigureParams.reInitEncodeParams.darHeight = Parameters.Height;
        ReconfigureParams.reInitEncodeParams.encodeConfig = &NewConfig;
        ReconfigureParams.reInitEncodeParams.maxEncodeWidth = Parameters.Width;
        ReconfigureParams.reInitEncodeParams.maxEncodeHeight = Parameters.Height;
        ReconfigureParams.reInitEncodeParams.bufferFormat = NvBufferFormat;
        ReconfigureParams.forceIDR = 1;
        ReconfigureParams.resetEncoder = 1;

        NVENCSTATUS Status = ReconfigureEncoder(Encoder, &ReconfigureParams);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCSession, Error, TEXT("NvEncReconfigureEncoder failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        EncodeConfig = NewConfig;
        InitializeParams = ReconfigureParams.reInitEncodeParams;
        CurrentParameters = Parameters;
        UE_LOG(LogNVENCSession, Verbose, TEXT("NVENC session reconfigured: %s"), *FNVENCParameterMapper::ToDebugString(CurrentParameters));
        return true;
#endif
    }

    void FNVENCSession::Flush()
    {
#if PLATFORM_WINDOWS
        if (!bIsInitialised)
        {
            return;
        }

        using TNvEncFlushEncoderQueue = NVENCSTATUS(NVENCAPI*)(void*, void*);
        TNvEncFlushEncoderQueue FlushEncoder = FunctionList.nvEncFlushEncoderQueue;
        if (FlushEncoder)
        {
            NVENCSTATUS Status = FlushEncoder(Encoder, nullptr);
            if (Status != NV_ENC_SUCCESS && Status != NV_ENC_ERR_NEED_MORE_INPUT)
            {
                UE_LOG(LogNVENCSession, Warning, TEXT("NvEncFlushEncoderQueue returned %s"), *FNVENCDefs::StatusToString(Status));
            }
        }
#endif
    }

    void FNVENCSession::Destroy()
    {
#if PLATFORM_WINDOWS
        if (!bIsOpen)
        {
            return;
        }

        using TNvEncDestroyEncoder = NVENCSTATUS(NVENCAPI*)(void*);
        TNvEncDestroyEncoder DestroyEncoder = FunctionList.nvEncDestroyEncoder;
        if (Encoder && DestroyEncoder)
        {
            NVENCSTATUS Status = DestroyEncoder(Encoder);
            if (Status != NV_ENC_SUCCESS)
            {
                UE_LOG(LogNVENCSession, Warning, TEXT("NvEncDestroyEncoder returned %s"), *FNVENCDefs::StatusToString(Status));
            }
        }

        Encoder = nullptr;
        Device = nullptr;
        bIsInitialised = false;
        bIsOpen = false;
        FunctionList = {};
#endif
        CurrentParameters = FNVENCParameters();
    }

    bool FNVENCSession::GetSequenceParams(TArray<uint8>& OutSequenceParams) const
    {
#if !PLATFORM_WINDOWS
        OutSequenceParams.Reset();
        return false;
#else
        OutSequenceParams.Reset();

        if (!bIsInitialised || !Encoder)
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("Cannot query NVENC sequence parameters – encoder is not initialised."));
            return false;
        }

        using TNvEncGetSequenceParams = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_SEQUENCE_PARAM_PAYLOAD*);
        TNvEncGetSequenceParams GetSequenceParamsFn = FunctionList.nvEncGetSequenceParams;
        if (!GetSequenceParamsFn)
        {
            UE_LOG(LogNVENCSession, Verbose, TEXT("nvEncGetSequenceParams unavailable – skipping codec config export."));
            return false;
        }

        OutSequenceParams.SetNumUninitialized(NV_MAX_SEQ_HDR_LEN);

        NV_ENC_SEQUENCE_PARAM_PAYLOAD Payload = {};
        Payload.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
        Payload.inBufferSize = OutSequenceParams.Num();
        Payload.spsppsBuffer = OutSequenceParams.GetData();

        NVENCSTATUS Status = GetSequenceParamsFn(Encoder, &Payload);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("nvEncGetSequenceParams failed: %s"), *FNVENCDefs::StatusToString(Status));
            OutSequenceParams.Reset();
            return false;
        }

        OutSequenceParams.SetNum(Payload.outSPSPPSPayloadSize);
        return Payload.outSPSPPSPayloadSize > 0;
#endif
    }
}

