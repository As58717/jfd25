// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Encoders/NVENCDefs.h"
#include "VideoEncoder.h"

namespace AVEncoder
{
    /**
     * Structure representing the high level NVENC configuration derived from an
     * Unreal Engine encoder layer configuration.  In the production encoder this
     * would be converted into NV_ENC_INITIALIZE_PARAMS / NV_ENC_CONFIG.
     */
    struct FNVENCParameters
    {
        ENVENCCodec Codec = ENVENCCodec::H264;
        ENVENCBufferFormat BufferFormat = ENVENCBufferFormat::NV12;
        uint32 Width = 0;
        uint32 Height = 0;
        uint32 Framerate = 0;
        int32 MaxBitrate = 0;
        int32 TargetBitrate = 0;
        int32 QPMin = -1;
        int32 QPMax = -1;
        FVideoEncoder::RateControlMode RateControlMode = FVideoEncoder::RateControlMode::CBR;
        FVideoEncoder::MultipassMode MultipassMode = FVideoEncoder::MultipassMode::FULL;
        bool bEnableLookahead = false;
        bool bEnableAdaptiveQuantization = false;
        uint32 GOPLength = 0;
    };

    /** Helper that performs the mapping from public API structures to NVENC friendly ones. */
    class FNVENCParameterMapper
    {
    public:
        static FNVENCParameters FromLayerConfig(const FVideoEncoder::FLayerConfig& Config, ENVENCCodec Codec, ENVENCBufferFormat Format);

        /** Creates a readable string representation of the parameter set. */
        static FString ToDebugString(const FNVENCParameters& Params);
    };
}

