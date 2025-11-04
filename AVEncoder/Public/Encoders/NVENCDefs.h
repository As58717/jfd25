// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace AVEncoder
{
    /** Enumerates the codecs exposed by the NVENC backend. */
    enum class ENVENCCodec : uint8
    {
        H264,
        HEVC,
    };

    /** Pixel formats supported by the NVENC entry points we expose in this trimmed build. */
    enum class ENVENCBufferFormat : uint8
    {
        NV12,
        P010,
        BGRA,
    };

    /** Simple view over the capabilities that we query from the runtime. */
    struct FNVENCCapabilities
    {
        bool bSupports10Bit = false;
        bool bSupportsBFrames = false;
        bool bSupportsYUV444 = false;
        bool bSupportsLookahead = false;
        bool bSupportsAdaptiveQuantization = false;
        int32 MaxWidth = 0;
        int32 MaxHeight = 0;
    };

    /** Handy helpers that keep commonly used constants and conversions together. */
    class FNVENCDefs
    {
    public:
        static const FGuid& CodecGuid(ENVENCCodec Codec);
        static const FGuid& PresetLowLatencyGuid();
        static const FGuid& PresetDefaultGuid();
        static const FGuid& TuningLatencyGuid();
        static const FGuid& TuningQualityGuid();

        static FString BufferFormatToString(ENVENCBufferFormat Format);
        static FString CodecToString(ENVENCCodec Codec);

        /** Converts well known NVENC status codes into log friendly text. */
        static FString StatusToString(int32 StatusCode);

        /** Returns the default API version we expect when creating the function list. */
        static uint32 GetDefaultAPIVersion();
    };
}

