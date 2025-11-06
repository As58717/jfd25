// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCAnnexB.h"

namespace OmniNVENC
{
    void FNVENCAnnexB::Reset()
    {
        CodecConfig.Reset();
    }

    void FNVENCAnnexB::SetCodecConfig(const TArray<uint8>& InData)
    {
        CodecConfig = InData;
    }
}

