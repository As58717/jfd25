// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_OMNI_NVENC

#include "CoreMinimal.h"
#include "Containers/Map.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include "Windows/HideWindowsPlatformTypes.h"
#include "nvEncodeAPI.h"
#endif

namespace OmniNVENC
{
    class FNVENCSession;
#if PLATFORM_WINDOWS
    class FNVENCInputD3D11;

    class FNVENCInputD3D12
    {
    public:
        FNVENCInputD3D12();

        /** Creates the D3D11-on-12 bridge used to surface D3D12 textures to NVENC. */
        bool Initialise(ID3D12Device* InDevice);

        /** Binds an NVENC session so that resources can be registered and mapped. */
        bool BindSession(FNVENCSession& InSession);

        void Shutdown();

        bool IsInitialised() const { return bIsInitialised; }
        bool IsSessionBound() const { return bSessionBound; }
        bool IsValid() const { return bIsInitialised && bSessionBound; }

        ID3D11Device* GetD3D11Device() const { return D3D11Device.GetReference(); }

        bool RegisterResource(ID3D12Resource* InRHITexture);
        void UnregisterResource(ID3D12Resource* InRHITexture);
        bool MapResource(ID3D12Resource* InRHITexture, NV_ENC_INPUT_PTR& OutMappedResource);
        void UnmapResource(NV_ENC_INPUT_PTR InMappedResource);

    private:
        struct FWrappedResource
        {
            TRefCountPtr<ID3D12Resource> D3D12Resource;
            TRefCountPtr<ID3D11Texture2D> D3D11Texture;
        };

        bool EnsureWrappedResource(ID3D12Resource* InResource, ID3D11Texture2D*& OutTexture);
        void ReleaseActiveMapping(NV_ENC_INPUT_PTR InMappedResource);

        TRefCountPtr<ID3D12Device> D3D12Device;
        TRefCountPtr<ID3D12CommandQueue> CommandQueue;
        TRefCountPtr<ID3D11Device> D3D11Device;
        TRefCountPtr<ID3D11DeviceContext> D3D11Context;
        TRefCountPtr<ID3D11On12Device> D3D11On12Device;
        FNVENCInputD3D11* D3D11Bridge = nullptr;
        FNVENCSession* Session = nullptr;
        TMap<ID3D12Resource*, FWrappedResource> WrappedResources;
        TMap<NV_ENC_INPUT_PTR, ID3D11Texture2D*> ActiveMappings;
        bool bIsInitialised = false;
        bool bSessionBound = false;
    };
#else
    class FNVENCInputD3D12
    {
    public:
        FNVENCInputD3D12() = default;

        bool Initialise(void*) { return false; }
        bool BindSession(FNVENCSession&) { return false; }
        void Shutdown() {}

        bool IsInitialised() const { return false; }
        bool IsSessionBound() const { return false; }
        bool IsValid() const { return false; }

        ID3D11Device* GetD3D11Device() const { return nullptr; }

        bool RegisterResource(void*) { return false; }
        void UnregisterResource(void*) {}
        bool MapResource(void*, NV_ENC_INPUT_PTR& OutMappedResource) { OutMappedResource = nullptr; return false; }
        void UnmapResource(NV_ENC_INPUT_PTR) {}
    };
#endif
}

#endif // WITH_OMNI_NVENC

