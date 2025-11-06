// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCInputD3D12.h"

#include "Logging/LogMacros.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#include <d3d12.h>
#include <d3d11on12.h>
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNVENCInputD3D12, Log, All);

namespace OmniNVENC
{
    namespace
    {
#if PLATFORM_WINDOWS
        void SafeRelease(IUnknown*& Object)
        {
            if (Object)
            {
                Object->Release();
                Object = nullptr;
            }
        }
#endif
    }

    FNVENCInputD3D12::FNVENCInputD3D12() = default;

    bool FNVENCInputD3D12::Initialise(ID3D12Device* InDevice)
    {
#if !PLATFORM_WINDOWS
        (void)InDevice;
        return false;
#else
        if (bIsInitialised)
        {
            return true;
        }

        if (!InDevice)
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Cannot initialise NVENC D3D12 bridge without a valid device."));
            return false;
        }

        if (D3D12Device)
        {
            D3D12Device->Release();
            D3D12Device = nullptr;
        }

        D3D12Device = InDevice;
        D3D12Device->AddRef();

        if (CommandQueue)
        {
            SafeRelease(reinterpret_cast<IUnknown*&>(CommandQueue));
        }

        D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
        QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        QueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        QueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        HRESULT Hr = D3D12Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&CommandQueue));
        if (FAILED(Hr) || !CommandQueue)
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to create D3D12 command queue for NVENC bridge (0x%08x)."), Hr);
            Shutdown();
            return false;
        }

        static const D3D_FEATURE_LEVEL FeatureLevels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        UINT CreateFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        SafeRelease(reinterpret_cast<IUnknown*&>(D3D11Device));
        SafeRelease(reinterpret_cast<IUnknown*&>(D3D11Context));
        SafeRelease(reinterpret_cast<IUnknown*&>(D3D11On12Device));

        Hr = D3D11On12CreateDevice(
            D3D12Device,
            CreateFlags,
            FeatureLevels,
            UE_ARRAY_COUNT(FeatureLevels),
            reinterpret_cast<IUnknown**>(&CommandQueue),
            1,
            0,
            &D3D11Device,
            &D3D11Context,
            nullptr);

        if (FAILED(Hr) || !D3D11Device || !D3D11Context)
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("D3D11On12CreateDevice failed for NVENC bridge (0x%08x)."), Hr);
            Shutdown();
            return false;
        }

        Hr = D3D11Device->QueryInterface(__uuidof(ID3D11On12Device), reinterpret_cast<void**>(&D3D11On12Device));
        if (FAILED(Hr) || !D3D11On12Device)
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to query ID3D11On12Device interface for NVENC bridge (0x%08x)."), Hr);
            Shutdown();
            return false;
        }

        UE_LOG(LogNVENCInputD3D12, Log, TEXT("NVENC D3D12 bridge initialised."));
        bIsInitialised = true;
        return true;
#endif
    }

    void FNVENCInputD3D12::Shutdown()
    {
#if PLATFORM_WINDOWS
        if (!bIsInitialised)
        {
            return;
        }

        if (D3D11On12Device)
        {
            for (auto& Pair : ResourceCache)
            {
                FWrappedResource& Wrapped = Pair.Value;
                if (Wrapped.D3D11Resource)
                {
                    ID3D11Resource* Resource = Wrapped.D3D11Resource;
                    D3D11On12Device->ReleaseWrappedResources(&Resource, 1);
                }

                SafeRelease(reinterpret_cast<IUnknown*&>(Wrapped.D3D11Texture));
                SafeRelease(reinterpret_cast<IUnknown*&>(Wrapped.D3D11Resource));
                SafeRelease(reinterpret_cast<IUnknown*&>(Wrapped.D3D12Resource));
            }
        }

        ResourceCache.Empty();

        if (D3D11Context)
        {
            D3D11Context->Flush();
        }

        SafeRelease(reinterpret_cast<IUnknown*&>(D3D11On12Device));
        SafeRelease(reinterpret_cast<IUnknown*&>(D3D11Context));
        SafeRelease(reinterpret_cast<IUnknown*&>(D3D11Device));
        SafeRelease(reinterpret_cast<IUnknown*&>(CommandQueue));
        SafeRelease(reinterpret_cast<IUnknown*&>(D3D12Device));
#endif

        bIsInitialised = false;
    }

    bool FNVENCInputD3D12::AcquireWrappedResource(ID3D12Resource* InResource, ID3D11Texture2D*& OutWrappedTexture)
    {
        OutWrappedTexture = nullptr;

#if !PLATFORM_WINDOWS
        (void)InResource;
        return false;
#else
        if (!bIsInitialised || !D3D11On12Device || !InResource)
        {
            return false;
        }

        FWrappedResource* Cached = ResourceCache.Find(InResource);
        if (!Cached)
        {
            FWrappedResource NewEntry;
            NewEntry.D3D12Resource = InResource;
            InResource->AddRef();

            D3D11_RESOURCE_FLAGS Flags = {};
            Flags.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            HRESULT Hr = D3D11On12Device->CreateWrappedResource(
                InResource,
                &Flags,
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_COMMON,
                __uuidof(ID3D11Resource),
                reinterpret_cast<void**>(&NewEntry.D3D11Resource));

            if (FAILED(Hr) || !NewEntry.D3D11Resource)
            {
                UE_LOG(LogNVENCInputD3D12, Error, TEXT("CreateWrappedResource failed for NVENC bridge (0x%08x)."), Hr);
                SafeRelease(reinterpret_cast<IUnknown*&>(NewEntry.D3D11Resource));
                SafeRelease(reinterpret_cast<IUnknown*&>(NewEntry.D3D12Resource));
                return false;
            }

            Hr = NewEntry.D3D11Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&NewEntry.D3D11Texture));
            if (FAILED(Hr) || !NewEntry.D3D11Texture)
            {
                UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to query wrapped ID3D11Texture2D (0x%08x)."), Hr);
                SafeRelease(reinterpret_cast<IUnknown*&>(NewEntry.D3D11Texture));
                SafeRelease(reinterpret_cast<IUnknown*&>(NewEntry.D3D11Resource));
                SafeRelease(reinterpret_cast<IUnknown*&>(NewEntry.D3D12Resource));
                return false;
            }

            Cached = &ResourceCache.Add(InResource, NewEntry);
        }

        if (!Cached->D3D11Resource || !Cached->D3D11Texture)
        {
            return false;
        }

        ID3D11Resource* Resource = Cached->D3D11Resource;
        D3D11On12Device->AcquireWrappedResources(&Resource, 1);
        OutWrappedTexture = Cached->D3D11Texture;
        return true;
#endif
    }

    void FNVENCInputD3D12::ReleaseWrappedResource(ID3D12Resource* InResource)
    {
#if PLATFORM_WINDOWS
        if (!bIsInitialised || !D3D11On12Device)
        {
            return;
        }

        if (FWrappedResource* Cached = ResourceCache.Find(InResource))
        {
            if (Cached->D3D11Resource)
            {
                ID3D11Resource* Resource = Cached->D3D11Resource;
                D3D11On12Device->ReleaseWrappedResources(&Resource, 1);
            }

            if (D3D11Context)
            {
                D3D11Context->Flush();
            }
        }
#else
        (void)InResource;
#endif
    }
}

