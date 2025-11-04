// Copyright Epic Games, Inc. All Rights Reserved.

#include "Encoders/VideoEncoderNVENC.h"

#include "Encoders/NVENCCommon.h"
#include "Encoders/NVEncodeAPILoader.h"
#include "Encoders/NVENCCaps.h"
#include "Encoders/NVENCParameters.h"
#include "Encoders/NVENCSession.h"
#include "Logging/LogMacros.h"
#include "VideoEncoderFactory.h"

DEFINE_LOG_CATEGORY_STATIC(LogVideoEncoderNVENC, Log, All);

namespace AVEncoder
{
    void FVideoEncoderNVENC::Register(FVideoEncoderFactory& InFactory)
    {
        auto RegisterCodec = [&InFactory](ECodecType CodecType, const FString& CodecName)
        {
            FVideoEncoderInfo EncoderInfo;
            EncoderInfo.Name = FString::Printf(TEXT("NVIDIA NVENC %s"), *CodecName);
            EncoderInfo.Type = EVideoEncoderType::Hardware;
            EncoderInfo.CodecType = CodecType;
            EncoderInfo.SupportedRateControlModes = (1 << static_cast<uint32>(ERateControlMode::CBR)) | (1 << static_cast<uint32>(ERateControlMode::VBR));
            EncoderInfo.SupportedFormats = (1 << static_cast<uint32>(EVideoFormat::NV12)) | (1 << static_cast<uint32>(EVideoFormat::P010)) | (1 << static_cast<uint32>(EVideoFormat::BGRA8));
            EncoderInfo.Capabilities = FVideoEncoderInfo::ECapabilities::SupportsDirectSubmission;

            InFactory.Register(EncoderInfo, []() { return MakeUnique<FVideoEncoderNVENC>(); });
        };

        RegisterCodec(ECodecType::H264, TEXT("H.264"));
        RegisterCodec(ECodecType::H265, TEXT("HEVC"));
    }

    FVideoEncoderNVENC::~FVideoEncoderNVENC()
    {
        Shutdown();
    }

    bool FVideoEncoderNVENC::Setup(TSharedRef<FVideoEncoderInput> InInput, const FLayerConfig& InLayerConfig)
    {
        if (!FNVEncodeAPILoader::Get().Load())
        {
            UE_LOG(LogVideoEncoderNVENC, Warning, TEXT("Failed to load NVENC runtime – encoder is unavailable."));
            return false;
        }

        if (!D3D11Input.Initialise())
        {
            UE_LOG(LogVideoEncoderNVENC, Warning, TEXT("Failed to initialise NVENC D3D11 input bridge."));
            return false;
        }

        Session = MakeUnique<FNVENCSession>();
        if (!Session->Open(ENVENCCodec::H264))
        {
            UE_LOG(LogVideoEncoderNVENC, Warning, TEXT("Failed to open NVENC session."));
            Session.Reset();
            return false;
        }

        CachedParameters = FNVENCParameterMapper::FromLayerConfig(InLayerConfig, ENVENCCodec::H264, ENVENCBufferFormat::NV12);
        Session->Initialize(CachedParameters);

        bIsReady = true;
        return FVideoEncoder::Setup(InInput, InLayerConfig);
    }

    void FVideoEncoderNVENC::Encode(const FVideoEncoderInputFrame* InFrame, const FEncodeOptions& InOptions)
    {
        if (!bIsReady)
        {
            UE_LOG(LogVideoEncoderNVENC, Verbose, TEXT("Ignoring encode request because encoder was not initialised."));
            return;
        }

        UE_LOG(LogVideoEncoderNVENC, Warning, TEXT("NVENC encode request ignored – trimmed build does not include the production encoder."));

        if (InFrame)
        {
            InFrame->Release();
        }
    }

    void FVideoEncoderNVENC::Flush()
    {
        if (Session)
        {
            Session->Flush();
        }
        else
        {
            UE_LOG(LogVideoEncoderNVENC, Verbose, TEXT("NVENC Flush requested without an active session."));
        }
    }

    void FVideoEncoderNVENC::Shutdown()
    {
        if (Session)
        {
            Session->Destroy();
            Session.Reset();
        }

        D3D11Input.Shutdown();
        AnnexB.Reset();
        Bitstream.Unlock();
        bIsReady = false;
    }
}

