// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace OmniNVENC
{
    class FNVENCInputD3D12
    {
    public:
        FNVENCInputD3D12();

        bool Initialise();
        void Shutdown();

        bool IsValid() const { return bIsInitialised; }

        bool RegisterResource(void* InRHITexture);
        void UnregisterResource(void* InRHITexture);
        bool MapResource(void* InRHITexture, void*& OutMappedResource);
        void UnmapResource(void* InMappedResource);

    private:
        bool bIsInitialised = false;
    };
}

