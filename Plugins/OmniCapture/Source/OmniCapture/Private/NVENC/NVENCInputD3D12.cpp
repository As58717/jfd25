// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCInputD3D12.h"

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCInputD3D12, Log, All);

namespace OmniNVENC
{
    FNVENCInputD3D12::FNVENCInputD3D12() = default;

    bool FNVENCInputD3D12::Initialise()
    {
        if (bIsInitialised)
        {
            return true;
        }

        UE_LOG(LogNVENCInputD3D12, Verbose, TEXT("Initialising NVENC D3D12 input bridge (placeholder)."));
        bIsInitialised = true;
        return true;
    }

    void FNVENCInputD3D12::Shutdown()
    {
        if (!bIsInitialised)
        {
            return;
        }

        UE_LOG(LogNVENCInputD3D12, Verbose, TEXT("Shutting down NVENC D3D12 input bridge."));
        bIsInitialised = false;
    }

    bool FNVENCInputD3D12::RegisterResource(void* InRHITexture)
    {
        UE_LOG(LogNVENCInputD3D12, Verbose, TEXT("RegisterResource called for %p (placeholder)."), InRHITexture);
        return bIsInitialised;
    }

    void FNVENCInputD3D12::UnregisterResource(void* InRHITexture)
    {
        UE_LOG(LogNVENCInputD3D12, Verbose, TEXT("UnregisterResource called for %p (placeholder)."), InRHITexture);
    }

    bool FNVENCInputD3D12::MapResource(void* InRHITexture, void*& OutMappedResource)
    {
        UE_LOG(LogNVENCInputD3D12, Verbose, TEXT("MapResource called for %p (placeholder)."), InRHITexture);
        OutMappedResource = InRHITexture;
        return bIsInitialised;
    }

    void FNVENCInputD3D12::UnmapResource(void* InMappedResource)
    {
        UE_LOG(LogNVENCInputD3D12, Verbose, TEXT("UnmapResource called for %p (placeholder)."), InMappedResource);
    }
}

