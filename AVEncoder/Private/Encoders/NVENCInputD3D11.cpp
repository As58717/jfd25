// Copyright Epic Games, Inc. All Rights Reserved.

#include "Encoders/NVENCInputD3D11.h"

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCInputD3D11, Log, All);

namespace AVEncoder
{
    FNVENCInputD3D11::FNVENCInputD3D11() = default;

    bool FNVENCInputD3D11::Initialise()
    {
        if (bIsInitialised)
        {
            return true;
        }

        UE_LOG(LogNVENCInputD3D11, Verbose, TEXT("Initialising NVENC D3D11 input bridge (placeholder)."));
        bIsInitialised = true;
        return true;
    }

    void FNVENCInputD3D11::Shutdown()
    {
        if (!bIsInitialised)
        {
            return;
        }

        UE_LOG(LogNVENCInputD3D11, Verbose, TEXT("Shutting down NVENC D3D11 input bridge."));
        bIsInitialised = false;
    }

    bool FNVENCInputD3D11::RegisterResource(void* InRHITexture)
    {
        UE_LOG(LogNVENCInputD3D11, Verbose, TEXT("RegisterResource called for %p (placeholder)."), InRHITexture);
        return bIsInitialised;
    }

    void FNVENCInputD3D11::UnregisterResource(void* InRHITexture)
    {
        UE_LOG(LogNVENCInputD3D11, Verbose, TEXT("UnregisterResource called for %p (placeholder)."), InRHITexture);
    }

    bool FNVENCInputD3D11::MapResource(void* InRHITexture, void*& OutMappedResource)
    {
        UE_LOG(LogNVENCInputD3D11, Verbose, TEXT("MapResource called for %p (placeholder)."), InRHITexture);
        OutMappedResource = InRHITexture;
        return bIsInitialised;
    }

    void FNVENCInputD3D11::UnmapResource(void* InMappedResource)
    {
        UE_LOG(LogNVENCInputD3D11, Verbose, TEXT("UnmapResource called for %p (placeholder)."), InMappedResource);
    }
}

