// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace OmniNVENC
{
    /** Utility responsible for packaging codec parameter sets into Annex B payloads. */
    class FNVENCAnnexB
    {
    public:
        /** Resets any cached state (e.g. SPS/PPS/VPS data). */
        void Reset();

        /** Returns cached codec configuration data to be emitted with the first packet. */
        const TArray<uint8>& GetCodecConfig() const { return CodecConfig; }

        /** Updates the cached configuration retrieved from NvEncGetSequenceParams. */
        void SetCodecConfig(const TArray<uint8>& InData);

    private:
        TArray<uint8> CodecConfig;
    };
}

