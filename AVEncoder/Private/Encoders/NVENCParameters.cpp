// Copyright Epic Games, Inc. All Rights Reserved.

#include "Encoders/NVENCParameters.h"

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCParameters, Log, All);

namespace AVEncoder
{
    namespace
    {
        uint32 DeriveFramerate(const FVideoEncoder::FLayerConfig& Config)
        {
            return Config.MaxFramerate == 0 ? 60u : Config.MaxFramerate;
        }

        ENVENCBufferFormat GuessFormat(ENVENCCodec Codec)
        {
            return Codec == ENVENCCodec::HEVC ? ENVENCBufferFormat::P010 : ENVENCBufferFormat::NV12;
        }
    }

    FNVENCParameters FNVENCParameterMapper::FromLayerConfig(const FVideoEncoder::FLayerConfig& Config, ENVENCCodec Codec, ENVENCBufferFormat Format)
    {
        FNVENCParameters Params;
        Params.Codec = Codec;
        Params.BufferFormat = Format;
        Params.Width = Config.Width;
        Params.Height = Config.Height;
        Params.Framerate = DeriveFramerate(Config);
        Params.MaxBitrate = Config.MaxBitrate;
        Params.TargetBitrate = Config.TargetBitrate;
        Params.QPMin = Config.QPMin;
        Params.QPMax = Config.QPMax;
        Params.RateControlMode = Config.RateControlMode;
        Params.MultipassMode = Config.MultipassMode;
        Params.bEnableAdaptiveQuantization = (Config.RateControlMode != FVideoEncoder::RateControlMode::CONSTQP);
        Params.bEnableLookahead = (Config.MultipassMode != FVideoEncoder::MultipassMode::DISABLED);
        Params.GOPLength = Config.MaxFramerate == 0 ? 0 : Config.MaxFramerate;

        if (Params.BufferFormat == ENVENCBufferFormat::NV12 && Codec == ENVENCCodec::HEVC)
        {
            // HEVC works best with P010 in HDR workflows but NV12 is the safest default when
            // colour depth requirements are unknown.
            Params.BufferFormat = GuessFormat(Codec);
        }

        UE_LOG(LogNVENCParameters, Verbose, TEXT("NVENC layer mapped to %s @ %ux%u %.2f fps bitrate %d/%d."),
            *FNVENCDefs::CodecToString(Codec),
            Params.Width,
            Params.Height,
            Params.Framerate == 0 ? 0.0 : static_cast<double>(Params.Framerate),
            Params.TargetBitrate,
            Params.MaxBitrate);

        return Params;
    }

    FString FNVENCParameterMapper::ToDebugString(const FNVENCParameters& Params)
    {
        return FString::Printf(TEXT("Codec=%s Format=%s %ux%u %u fps Bitrate=%d/%d QP=[%d,%d] RC=%d MP=%d AQ=%s LA=%s GOP=%u"),
            *FNVENCDefs::CodecToString(Params.Codec),
            *FNVENCDefs::BufferFormatToString(Params.BufferFormat),
            Params.Width,
            Params.Height,
            Params.Framerate,
            Params.TargetBitrate,
            Params.MaxBitrate,
            Params.QPMin,
            Params.QPMax,
            static_cast<int32>(Params.RateControlMode),
            static_cast<int32>(Params.MultipassMode),
            Params.bEnableAdaptiveQuantization ? TEXT("on") : TEXT("off"),
            Params.bEnableLookahead ? TEXT("on") : TEXT("off"),
            Params.GOPLength);
    }
}

