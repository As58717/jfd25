// Copyright Epic Games, Inc. All Rights Reserved.

#include "Encoders/NVENCCaps.h"

#include "Encoders/NVEncodeAPILoader.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCCaps, Log, All);

namespace AVEncoder
{
    bool FNVENCCaps::Query(ENVENCCodec Codec, FNVENCCapabilities& OutCapabilities)
    {
        OutCapabilities = FNVENCCapabilities();

        FNVEncodeAPILoader& Loader = FNVEncodeAPILoader::Get();
        if (!Loader.Load())
        {
            UE_LOG(LogNVENCCaps, Warning, TEXT("NVENC capability query failed – loader was unable to resolve the runtime."));
            return false;
        }

        UE_LOG(LogNVENCCaps, Verbose, TEXT("NVENC capability probing is not available in this trimmed build. Returning defaults for %s."), *FNVENCDefs::CodecToString(Codec));
        return false;
    }

    FString FNVENCCaps::ToDebugString(const FNVENCCapabilities& Caps)
    {
        return FString::Printf(TEXT("10bit=%s BFrames=%s YUV444=%s Lookahead=%s AQ=%s MaxResolution=%dx%d"),
            Caps.bSupports10Bit ? TEXT("yes") : TEXT("no"),
            Caps.bSupportsBFrames ? TEXT("yes") : TEXT("no"),
            Caps.bSupportsYUV444 ? TEXT("yes") : TEXT("no"),
            Caps.bSupportsLookahead ? TEXT("yes") : TEXT("no"),
            Caps.bSupportsAdaptiveQuantization ? TEXT("yes") : TEXT("no"),
            Caps.MaxWidth,
            Caps.MaxHeight);
    }
}

