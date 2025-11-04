// Copyright Epic Games, Inc. All Rights Reserved.

#include "Encoders/NVENCSession.h"

#include "Encoders/NVEncodeAPILoader.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCSession, Log, All);

namespace AVEncoder
{
    bool FNVENCSession::Open(ENVENCCodec Codec)
    {
        if (bIsOpen)
        {
            return true;
        }

        if (!FNVEncodeAPILoader::Get().Load())
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("Failed to open NVENC session for codec %s – runtime is unavailable."), *FNVENCDefs::CodecToString(Codec));
            return false;
        }

        bIsOpen = true;
        CurrentParameters.Codec = Codec;
        return true;
    }

    bool FNVENCSession::Initialize(const FNVENCParameters& Parameters)
    {
        if (!bIsOpen)
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("Cannot initialise NVENC session – encoder is not open."));
            return false;
        }

        CurrentParameters = Parameters;
        bIsInitialised = true;
        UE_LOG(LogNVENCSession, Verbose, TEXT("NVENC session initialised: %s"), *FNVENCParameterMapper::ToDebugString(CurrentParameters));
        return true;
    }

    bool FNVENCSession::Reconfigure(const FNVENCParameters& Parameters)
    {
        if (!bIsInitialised)
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("Cannot reconfigure NVENC session – encoder has not been initialised."));
            return false;
        }

        CurrentParameters = Parameters;
        UE_LOG(LogNVENCSession, Verbose, TEXT("NVENC session reconfigured: %s"), *FNVENCParameterMapper::ToDebugString(CurrentParameters));
        return true;
    }

    void FNVENCSession::Flush()
    {
        UE_LOG(LogNVENCSession, Verbose, TEXT("NVENC session flush requested."));
    }

    void FNVENCSession::Destroy()
    {
        if (!bIsOpen)
        {
            return;
        }

        UE_LOG(LogNVENCSession, Verbose, TEXT("NVENC session destroyed."));
        bIsInitialised = false;
        bIsOpen = false;
        CurrentParameters = FNVENCParameters();
    }
}

