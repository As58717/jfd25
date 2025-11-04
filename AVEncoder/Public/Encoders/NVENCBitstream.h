// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace AVEncoder
{
    struct FNVENCEncodedPacket
    {
        TArray<uint8> Data;
        bool bKeyFrame = false;
        uint64 Timestamp = 0;
    };

    /** Utility that wraps the nvEncLockBitstream/nvEncUnlockBitstream pair. */
    class FNVENCBitstream
    {
    public:
        bool Lock(void*& OutBitstreamBuffer, int32& OutSizeInBytes);
        void Unlock();

        bool ExtractPacket(FNVENCEncodedPacket& OutPacket);

    private:
        bool bIsLocked = false;
    };
}

