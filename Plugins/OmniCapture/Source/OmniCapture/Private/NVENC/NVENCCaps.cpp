// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCCaps.h"

#include "NVENC/NVEncodeAPILoader.h"
#include "NVENC/NVENCDefs.h"
#include "NVENC/NVENCSession.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeExit.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNVENCCaps, Log, All);

namespace OmniNVENC
{
    bool FNVENCCaps::Query(ENVENCCodec Codec, FNVENCCapabilities& OutCapabilities)
    {
        OutCapabilities = FNVENCCapabilities();

        FNVEncodeAPILoader& Loader = FNVEncodeAPILoader::Get();
        if (!Loader.Load())
        {
            UE_LOG(LogNVENCCaps, Warning, TEXT("NVENC capability query failed – loader was unable to resolve the runtime."));
            return false;
        }

#if !PLATFORM_WINDOWS
        UE_LOG(LogNVENCCaps, Warning, TEXT("NVENC capability probing is only supported on Windows."));
        return false;
#else
        TRefCountPtr<ID3D11Device> Device;
        TRefCountPtr<ID3D11DeviceContext> Context;

        UINT DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        const D3D_FEATURE_LEVEL FeatureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL CreatedLevel = D3D_FEATURE_LEVEL_11_0;

        HRESULT CreateDeviceResult = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            DeviceFlags,
            FeatureLevels,
            UE_ARRAY_COUNT(FeatureLevels),
            D3D11_SDK_VERSION,
            Device.GetInitReference(),
            &CreatedLevel,
            Context.GetInitReference());

        if (FAILED(CreateDeviceResult))
        {
            UE_LOG(LogNVENCCaps, Warning, TEXT("Unable to create temporary D3D11 device for NVENC capability query (0x%08x)."), CreateDeviceResult);
            return false;
        }

        OmniNVENC::FNVENCSession Session;
        if (!Session.Open(Codec, Device.GetReference(), NV_ENC_DEVICE_TYPE_DIRECTX))
        {
            UE_LOG(LogNVENCCaps, Warning, TEXT("NVENC capability query failed – unable to open session for %s."), *FNVENCDefs::CodecToString(Codec));
            return false;
        }

        auto CloseSession = MakeScopeExit([&Session]() { Session.Destroy(); });

        using TNvEncGetEncodeCaps = NVENCSTATUS(NVENCAPI*)(void*, GUID, NV_ENC_CAPS_PARAM*, int*);
        TNvEncGetEncodeCaps GetEncodeCapsFn = Session.GetFunctionList().nvEncGetEncodeCaps;
        if (!GetEncodeCapsFn)
        {
            UE_LOG(LogNVENCCaps, Warning, TEXT("NVENC runtime does not expose NvEncGetEncodeCaps."));
            return false;
        }

        auto ToWindowsGuid = [](const FGuid& InGuid)
        {
            GUID Guid;
            Guid.Data1 = static_cast<uint32>(InGuid.A);
            Guid.Data2 = static_cast<uint16>((static_cast<uint32>(InGuid.B) >> 16) & 0xFFFF);
            Guid.Data3 = static_cast<uint16>(static_cast<uint32>(InGuid.B) & 0xFFFF);

            const uint32 C = static_cast<uint32>(InGuid.C);
            const uint32 D = static_cast<uint32>(InGuid.D);

            Guid.Data4[0] = static_cast<uint8>((C >> 24) & 0xFF);
            Guid.Data4[1] = static_cast<uint8>((C >> 16) & 0xFF);
            Guid.Data4[2] = static_cast<uint8>((C >> 8) & 0xFF);
            Guid.Data4[3] = static_cast<uint8>(C & 0xFF);
            Guid.Data4[4] = static_cast<uint8>((D >> 24) & 0xFF);
            Guid.Data4[5] = static_cast<uint8>((D >> 16) & 0xFF);
            Guid.Data4[6] = static_cast<uint8>((D >> 8) & 0xFF);
            Guid.Data4[7] = static_cast<uint8>(D & 0xFF);
            return Guid;
        };

        const GUID CodecGuid = ToWindowsGuid(FNVENCDefs::CodecGuid(Codec));

        auto QueryCapability = [&](NV_ENC_CAPS Capability, int32 DefaultValue = 0) -> int32
        {
            NV_ENC_CAPS_PARAM CapsParam = {};
            CapsParam.version = NV_ENC_CAPS_PARAM_VER;
            CapsParam.capsToQuery = Capability;

            int CapsValue = DefaultValue;
            NVENCSTATUS Status = GetEncodeCapsFn(Session.GetEncoderHandle(), CodecGuid, &CapsParam, &CapsValue);
            if (Status != NV_ENC_SUCCESS)
            {
                UE_LOG(LogNVENCCaps, Verbose, TEXT("NvEncGetEncodeCaps(%d) returned %s"), static_cast<int32>(Capability), *FNVENCDefs::StatusToString(Status));
                return DefaultValue;
            }
            return CapsValue;
        };

        OutCapabilities.bSupports10Bit = QueryCapability(NV_ENC_CAPS_SUPPORT_10BIT_ENCODE) != 0;
        OutCapabilities.bSupportsBFrames = QueryCapability(NV_ENC_CAPS_NUM_MAX_BFRAMES) > 0;
        OutCapabilities.bSupportsYUV444 = QueryCapability(NV_ENC_CAPS_SUPPORT_YUV444_ENCODE) != 0;
        OutCapabilities.bSupportsLookahead = QueryCapability(NV_ENC_CAPS_SUPPORT_LOOKAHEAD) != 0;
        OutCapabilities.bSupportsAdaptiveQuantization = QueryCapability(NV_ENC_CAPS_SUPPORT_TEMPORAL_AQ) != 0;
        OutCapabilities.MaxWidth = QueryCapability(NV_ENC_CAPS_WIDTH_MAX);
        OutCapabilities.MaxHeight = QueryCapability(NV_ENC_CAPS_HEIGHT_MAX);

        UE_LOG(LogNVENCCaps, Verbose, TEXT("Queried NVENC caps for %s: %s"), *FNVENCDefs::CodecToString(Codec), *FNVENCCaps::ToDebugString(OutCapabilities));
        return true;
#endif
    }

    FString FNVENCCaps::ToDebugString(const FNVENCCapabilities& Caps)
    {
        return FString::Printf(TEXT("10bit=%s BFrames=%s YUV444=%s Lookahead=%s AQ=%s MaxResolution=%dx%d"),
            Caps.bSupports10Bit ? TEXT("yes") : TEXT("no"),
            Caps.bSupportsBFrames ? TEXT("yes") : TEXT("no"),
            Caps.bSupportsYUV444 ? TEXT("yes") : TEXT("no"),
            Caps.bSupportsLookahead ? TEXT("yes") : TEXT("no"),
            Caps.bSupportsAdaptiveQuantization ? TEXT("yes") : TEXT("no"),
            Caps.MaxWidth,
            Caps.MaxHeight);
    }
}

