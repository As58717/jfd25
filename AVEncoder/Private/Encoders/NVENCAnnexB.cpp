// Copyright Epic Games, Inc. All Rights Reserved.

#include "Encoders/NVENCAnnexB.h"

namespace AVEncoder
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

