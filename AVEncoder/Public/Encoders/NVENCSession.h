// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Encoders/NVENCParameters.h"

namespace AVEncoder
{
    /**
     * Thin wrapper that models the lifecycle of an NVENC encoder instance.
     * The full implementation is proprietary but the skeleton is useful for
     * integration tests and for keeping the public API stable.
     */
    class FNVENCSession
    {
    public:
        FNVENCSession() = default;

        bool Open(ENVENCCodec Codec);
        bool Initialize(const FNVENCParameters& Parameters);
        bool Reconfigure(const FNVENCParameters& Parameters);
        void Flush();
        void Destroy();

        bool IsOpen() const { return bIsOpen; }
        bool IsInitialised() const { return bIsInitialised; }

        const FNVENCParameters& GetParameters() const { return CurrentParameters; }

    private:
        bool bIsOpen = false;
        bool bIsInitialised = false;
        FNVENCParameters CurrentParameters;
    };
}

