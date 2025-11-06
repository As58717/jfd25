// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCInputD3D12.h"

#if WITH_OMNI_NVENC

#include "NVENC/NVENCInputD3D11.h"
#include "NVENC/NVENCSession.h"
#include "Logging/LogMacros.h"

#if PLATFORM_WINDOWS
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNVENCInputD3D12, Log, All);

namespace OmniNVENC
{
#if PLATFORM_WINDOWS
    FNVENCInputD3D12::FNVENCInputD3D12() = default;

    bool FNVENCInputD3D12::Initialise(ID3D12Device* InDevice)
    {
        if (bIsInitialised)
        {
            return true;
        }

        if (!InDevice)
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Cannot initialise NVENC D3D12 bridge without a valid device."));
            return false;
        }

        D3D12Device = InDevice;

        D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
        QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        QueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        QueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        HRESULT QueueResult = D3D12Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(CommandQueue.GetInitReference()));
        if (FAILED(QueueResult))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to create D3D12 command queue for NVENC bridge (0x%08x)."), QueueResult);
            Shutdown();
            return false;
        }

        ID3D12CommandQueue* CommandQueues[] = { CommandQueue.GetReference() };

        const D3D_FEATURE_LEVEL FeatureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        UINT DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        HRESULT BridgeResult = D3D11On12CreateDevice(
            D3D12Device.GetReference(),
            DeviceFlags,
            FeatureLevels,
            UE_ARRAY_COUNT(FeatureLevels),
            reinterpret_cast<IUnknown**>(CommandQueues),
            UE_ARRAY_COUNT(CommandQueues),
            0,
            D3D11Device.GetInitReference(),
            D3D11Context.GetInitReference(),
            nullptr);

        if (FAILED(BridgeResult))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("D3D11On12CreateDevice failed (0x%08x)."), BridgeResult);
            Shutdown();
            return false;
        }

        HRESULT QueryResult = D3D11Device->QueryInterface(IID_PPV_ARGS(D3D11On12Device.GetInitReference()));
        if (FAILED(QueryResult))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to acquire ID3D11On12Device interface (0x%08x)."), QueryResult);
            Shutdown();
            return false;
        }

        bIsInitialised = true;
        return true;
    }

    bool FNVENCInputD3D12::BindSession(FNVENCSession& InSession)
    {
        if (!bIsInitialised)
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Cannot bind NVENC session – D3D12 bridge is not initialised."));
            return false;
        }

        if (bSessionBound && Session == &InSession)
        {
            return true;
        }

        if (!D3D11Bridge)
        {
            D3D11Bridge = new FNVENCInputD3D11();
        }

        if (!D3D11Bridge->Initialise(D3D11Device.GetReference(), InSession))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to initialise NVENC D3D11 bridge for D3D12 input."));
            return false;
        }

        Session = &InSession;
        bSessionBound = true;
        return true;
    }

    void FNVENCInputD3D12::Shutdown()
    {
        if (!bIsInitialised)
        {
            return;
        }

        {
            TArray<NV_ENC_INPUT_PTR> MappingKeys;
            ActiveMappings.GenerateKeyArray(MappingKeys);
            for (NV_ENC_INPUT_PTR Mapping : MappingKeys)
            {
                ReleaseActiveMapping(Mapping);
            }
            ActiveMappings.Empty();
        }

        for (auto It = WrappedResources.CreateIterator(); It; ++It)
        {
            if (D3D11Bridge)
            {
                D3D11Bridge->UnregisterResource(It.Value().D3D11Texture.GetReference());
            }
        }
        WrappedResources.Empty();

        if (D3D11Bridge)
        {
            D3D11Bridge->Shutdown();
            delete D3D11Bridge;
            D3D11Bridge = nullptr;
        }

        if (D3D11Context.IsValid())
        {
            D3D11Context->Flush();
        }

        D3D11On12Device = nullptr;
        D3D11Context = nullptr;
        D3D11Device = nullptr;
        CommandQueue = nullptr;

        D3D12Device = nullptr;

        Session = nullptr;
        bSessionBound = false;
        bIsInitialised = false;
    }

    bool FNVENCInputD3D12::RegisterResource(ID3D12Resource* InRHITexture)
    {
        if (!IsValid() || !InRHITexture)
        {
            return false;
        }

        ID3D11Texture2D* WrappedTexture = nullptr;
        return EnsureWrappedResource(InRHITexture, WrappedTexture);
    }

    void FNVENCInputD3D12::UnregisterResource(ID3D12Resource* InRHITexture)
    {
        if (!IsValid() || !InRHITexture)
        {
            return;
        }

        FWrappedResource Resource;
        if (!WrappedResources.RemoveAndCopyValue(InRHITexture, Resource))
        {
            return;
        }

        if (D3D11Bridge)
        {
            D3D11Bridge->UnregisterResource(Resource.D3D11Texture.GetReference());
        }

        if (D3D11On12Device.IsValid() && Resource.D3D11Texture.IsValid())
        {
            ID3D11Resource* const ReleaseResource[] = { Resource.D3D11Texture.GetReference() };
            D3D11On12Device->ReleaseWrappedResources(ReleaseResource, 1);
        }
    }

    bool FNVENCInputD3D12::MapResource(ID3D12Resource* InRHITexture, NV_ENC_INPUT_PTR& OutMappedResource)
    {
        OutMappedResource = nullptr;

        if (!IsValid() || !Session || !Session->IsInitialised() || !InRHITexture)
        {
            return false;
        }

        ID3D11Texture2D* WrappedTexture = nullptr;
        if (!EnsureWrappedResource(InRHITexture, WrappedTexture))
        {
            return false;
        }

        if (!D3D11On12Device.IsValid())
        {
            return false;
        }

        ID3D11Resource* const AcquireResources[] = { WrappedTexture };
        D3D11On12Device->AcquireWrappedResources(AcquireResources, 1);

        NV_ENC_INPUT_PTR NvResource = nullptr;
        if (!D3D11Bridge->MapResource(WrappedTexture, NvResource))
        {
            D3D11On12Device->ReleaseWrappedResources(AcquireResources, 1);
            return false;
        }

        ActiveMappings.Add(NvResource, WrappedTexture);
        OutMappedResource = NvResource;
        return true;
    }

    void FNVENCInputD3D12::UnmapResource(NV_ENC_INPUT_PTR InMappedResource)
    {
        ReleaseActiveMapping(InMappedResource);
    }

    bool FNVENCInputD3D12::EnsureWrappedResource(ID3D12Resource* InResource, ID3D11Texture2D*& OutTexture)
    {
        OutTexture = nullptr;

        if (!IsValid() || !InResource)
        {
            return false;
        }

        if (FWrappedResource* Existing = WrappedResources.Find(InResource))
        {
            OutTexture = Existing->D3D11Texture.GetReference();
            return OutTexture != nullptr;
        }

        if (!D3D11On12Device.IsValid())
        {
            return false;
        }

        D3D11_RESOURCE_FLAGS Flags = {};
        TRefCountPtr<ID3D11Resource> WrappedResource;
        HRESULT Result = D3D11On12Device->CreateWrappedResource(
            InResource,
            &Flags,
            D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
            D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
            IID_PPV_ARGS(WrappedResource.GetInitReference()));

        if (FAILED(Result))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("CreateWrappedResource failed for %p (0x%08x)."), InResource, Result);
            return false;
        }

        TRefCountPtr<ID3D11Texture2D> WrappedTexture;
        Result = WrappedResource->QueryInterface(IID_PPV_ARGS(WrappedTexture.GetInitReference()));
        if (FAILED(Result))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to query ID3D11Texture2D for wrapped resource (0x%08x)."), Result);
            return false;
        }

        FWrappedResource ResourceEntry;
        ResourceEntry.D3D12Resource = InResource;
        ResourceEntry.D3D11Texture = WrappedTexture;

        if (!D3D11Bridge->RegisterResource(WrappedTexture.GetReference()))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to register wrapped D3D12 texture with NVENC."));
            return false;
        }

        WrappedResources.Add(InResource, ResourceEntry);
        OutTexture = WrappedTexture.GetReference();
        return true;
    }

    void FNVENCInputD3D12::ReleaseActiveMapping(NV_ENC_INPUT_PTR InMappedResource)
    {
        if (!InMappedResource || !D3D11Bridge)
        {
            return;
        }

        ID3D11Texture2D** WrappedTexturePtr = ActiveMappings.Find(InMappedResource);
        if (!WrappedTexturePtr || !*WrappedTexturePtr)
        {
            return;
        }

        ID3D11Texture2D* WrappedTexture = *WrappedTexturePtr;
        ID3D11Resource* const ReleaseResources[] = { WrappedTexture };

        D3D11Bridge->UnmapResource(InMappedResource);

        if (D3D11On12Device.IsValid())
        {
            D3D11On12Device->ReleaseWrappedResources(ReleaseResources, 1);
        }

        if (D3D11Context.IsValid())
        {
            D3D11Context->Flush();
        }

        ActiveMappings.Remove(InMappedResource);
    }
#else
    FNVENCInputD3D12::FNVENCInputD3D12() = default;
#endif
#endif // WITH_OMNI_NVENC
}

