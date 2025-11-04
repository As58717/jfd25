// Copyright Epic Games, Inc. All Rights Reserved.

#include "Encoders/NVENCBitstream.h"

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCBitstream, Log, All);

namespace AVEncoder
{
    bool FNVENCBitstream::Lock(void*& OutBitstreamBuffer, int32& OutSizeInBytes)
    {
        if (bIsLocked)
        {
            UE_LOG(LogNVENCBitstream, Warning, TEXT("Bitstream already locked."));
            OutBitstreamBuffer = nullptr;
            OutSizeInBytes = 0;
            return false;
        }

        UE_LOG(LogNVENCBitstream, Verbose, TEXT("Locking NVENC bitstream (placeholder)."));
        bIsLocked = true;
        OutBitstreamBuffer = nullptr;
        OutSizeInBytes = 0;
        return true;
    }

    void FNVENCBitstream::Unlock()
    {
        if (!bIsLocked)
        {
            return;
        }

        UE_LOG(LogNVENCBitstream, Verbose, TEXT("Unlocking NVENC bitstream (placeholder)."));
        bIsLocked = false;
    }

    bool FNVENCBitstream::ExtractPacket(FNVENCEncodedPacket& OutPacket)
    {
        if (!bIsLocked)
        {
            OutPacket = FNVENCEncodedPacket();
            UE_LOG(LogNVENCBitstream, Warning, TEXT("Attempted to extract NVENC packet without a locked bitstream."));
            return false;
        }

        OutPacket = FNVENCEncodedPacket();
        UE_LOG(LogNVENCBitstream, Verbose, TEXT("ExtractPacket placeholder invoked."));
        return false;
    }
}

