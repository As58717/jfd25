// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Containers/Map.h"

struct ID3D11Device;
struct ID3D11Texture2D;
struct ID3D11Resource;
struct ID3D11On12Device;
struct ID3D11DeviceContext;
struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12CommandQueue;

namespace OmniNVENC
{
    /**
     * Bridges D3D12 textures into the D3D11-based NVENC path by wrapping them
     * through an ID3D11On12 device.  The wrapped D3D11 textures are cached so
     * we can re-use registrations across frames without extra allocations.
     */
    class FNVENCInputD3D12
    {
    public:
        FNVENCInputD3D12();

        /** Creates the D3D11-on-12 bridge for the provided device. */
        bool Initialise(ID3D12Device* InDevice);
        void Shutdown();

        bool IsValid() const { return bIsInitialised; }

        /** Returns the bridged D3D11 device that should back the NVENC session. */
        ID3D11Device* GetD3D11Device() const { return D3D11Device; }

        /**
         * Wraps the supplied D3D12 resource and returns the cached D3D11 view.
         * Acquire/Release must be balanced per frame.
         */
        bool AcquireWrappedResource(ID3D12Resource* InResource, ID3D11Texture2D*& OutWrappedTexture);
        void ReleaseWrappedResource(ID3D12Resource* InResource);

    private:
        struct FWrappedResource
        {
            ID3D12Resource* D3D12Resource = nullptr;
            ID3D11Resource* D3D11Resource = nullptr;
            ID3D11Texture2D* D3D11Texture = nullptr;
        };

        bool bIsInitialised = false;
        ID3D12Device* D3D12Device = nullptr;
        ID3D12CommandQueue* CommandQueue = nullptr;
        ID3D11Device* D3D11Device = nullptr;
        ID3D11DeviceContext* D3D11Context = nullptr;
        ID3D11On12Device* D3D11On12Device = nullptr;
        TMap<ID3D12Resource*, FWrappedResource> ResourceCache;
    };
}

