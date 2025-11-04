// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Encoders/NVENCDefs.h"

namespace AVEncoder
{
    /**
     * Helper responsible for probing a runtime NVENC instance for optional
     * capabilities.  The real implementation lives in proprietary code but we
     * keep the structure here so higher level systems can be unit tested.
     */
    class FNVENCCaps
    {
    public:
        /**
         * Populates the provided capability structure.  Returns true if the
         * runtime answered our query, false if the probe failed (e.g. missing
         * API entry points or running on an unsupported platform).
         */
        static bool Query(ENVENCCodec Codec, FNVENCCapabilities& OutCapabilities);

        /** Serialises the capability structure into a log friendly string. */
        static FString ToDebugString(const FNVENCCapabilities& Caps);
    };
}

