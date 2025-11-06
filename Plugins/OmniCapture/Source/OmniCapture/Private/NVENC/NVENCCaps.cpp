// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCCaps.h"

#include "NVENC/NVENCDefs.h"
#include "NVENC/NVENCSession.h"
#include "Logging/LogMacros.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#include <d3d11.h>
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNVENCCaps, Log, All);

namespace OmniNVENC
{
    namespace
    {
#if PLATFORM_WINDOWS
        template <typename TFunc>
        bool ValidateFunction(const ANSICHAR* Name, TFunc* Function)
        {
            if (!Function)
            {
                UE_LOG(LogNVENCCaps, Error, TEXT("Required NVENC export '%s' is missing for capability query."), ANSI_TO_TCHAR(Name));
                return false;
            }
            return true;
        }

        GUID ToWindowsGuid(const FGuid& InGuid)
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
        }

        int32 QueryCap(void* Encoder, const NV_ENCODE_API_FUNCTION_LIST& Functions, const GUID& CodecGuid, uint32 CapId)
        {
            using TNvEncGetEncodeCaps = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_CAPS_PARAM*, int*);
            TNvEncGetEncodeCaps GetCaps = Functions.nvEncGetEncodeCaps;
            if (!ValidateFunction("NvEncGetEncodeCaps", GetCaps))
            {
                return 0;
            }

            NV_ENC_CAPS_PARAM Caps = {};
            Caps.version = NV_ENC_CAPS_PARAM_VER;
            Caps.capsToQuery = CapId;
            Caps.encodeGUID = CodecGuid;

            int Value = 0;
            NVENCSTATUS Status = GetCaps(Encoder, &Caps, &Value);
            if (Status != NV_ENC_SUCCESS)
            {
                UE_LOG(LogNVENCCaps, Verbose, TEXT("nvEncGetEncodeCaps(%u) failed: %s"), CapId, *FNVENCDefs::StatusToString(Status));
                return 0;
            }
            return Value;
        }
#endif
    }

    bool FNVENCCaps::Query(ENVENCCodec Codec, FNVENCCapabilities& OutCapabilities)
    {
        OutCapabilities = FNVENCCapabilities();

#if !PLATFORM_WINDOWS
        (void)Codec;
        return false;
#else
        TRefCountPtr<ID3D11Device> Device;
        TRefCountPtr<ID3D11DeviceContext> Context;

        static const D3D_FEATURE_LEVEL FeatureLevels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        UINT CreateFlags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        HRESULT Hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            CreateFlags,
            FeatureLevels,
            UE_ARRAY_COUNT(FeatureLevels),
            D3D11_SDK_VERSION,
            Device.GetInitReference(),
            nullptr,
            Context.GetInitReference());

        if (FAILED(Hr) || !Device.IsValid())
        {
            UE_LOG(LogNVENCCaps, Warning, TEXT("D3D11CreateDevice failed while probing NVENC capabilities (0x%08x)."), Hr);
            return false;
        }

        FNVENCSession Session;
        if (!Session.Open(Codec, Device.GetReference(), NV_ENC_DEVICE_TYPE_DIRECTX))
        {
            UE_LOG(LogNVENCCaps, Warning, TEXT("Unable to open NVENC session for capability query."));
            return false;
        }

        const NV_ENCODE_API_FUNCTION_LIST& Functions = Session.GetFunctionList();
        void* EncoderHandle = Session.GetEncoderHandle();
        if (!EncoderHandle)
        {
            UE_LOG(LogNVENCCaps, Warning, TEXT("Capability query failed – encoder handle is null."));
            return false;
        }

        using TNvEncGetEncodeGUIDCount = NVENCSTATUS(NVENCAPI*)(void*, uint32*);
        using TNvEncGetEncodeGUIDs = NVENCSTATUS(NVENCAPI*)(void*, GUID*, uint32, uint32*);
        using TNvEncGetInputFormatCount = NVENCSTATUS(NVENCAPI*)(void*, GUID, uint32*);
        using TNvEncGetInputFormats = NVENCSTATUS(NVENCAPI*)(void*, GUID, NV_ENC_BUFFER_FORMAT*, uint32, uint32*);

        TNvEncGetEncodeGUIDCount GetGuidCount = Functions.nvEncGetEncodeGUIDCount;
        TNvEncGetEncodeGUIDs GetGuids = Functions.nvEncGetEncodeGUIDs;
        TNvEncGetInputFormatCount GetFormatCount = Functions.nvEncGetInputFormatCount;
        TNvEncGetInputFormats GetFormats = Functions.nvEncGetInputFormats;

        if (!ValidateFunction("NvEncGetEncodeGUIDCount", GetGuidCount) ||
            !ValidateFunction("NvEncGetEncodeGUIDs", GetGuids) ||
            !ValidateFunction("NvEncGetInputFormatCount", GetFormatCount) ||
            !ValidateFunction("NvEncGetInputFormats", GetFormats))
        {
            return false;
        }

        GUID CodecGuid = ToWindowsGuid(FNVENCDefs::CodecGuid(Codec));

        uint32 GuidCount = 0;
        NVENCSTATUS Status = GetGuidCount(EncoderHandle, &GuidCount);
        if (Status != NV_ENC_SUCCESS || GuidCount == 0)
        {
            UE_LOG(LogNVENCCaps, Warning, TEXT("nvEncGetEncodeGUIDCount failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        TArray<GUID> SupportedGuids;
        SupportedGuids.SetNumUninitialized(GuidCount);
        uint32 Retrieved = GuidCount;
        Status = GetGuids(EncoderHandle, SupportedGuids.GetData(), GuidCount, &Retrieved);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCCaps, Warning, TEXT("nvEncGetEncodeGUIDs failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        bool bCodecSupported = false;
        for (uint32 Index = 0; Index < Retrieved; ++Index)
        {
            if (FMemory::Memcmp(&SupportedGuids[Index], &CodecGuid, sizeof(GUID)) == 0)
            {
                bCodecSupported = true;
                break;
            }
        }

        if (!bCodecSupported)
        {
            UE_LOG(LogNVENCCaps, Warning, TEXT("NVENC codec %s is not supported on this adapter."), *FNVENCDefs::CodecToString(Codec));
            return false;
        }

        uint32 FormatCount = 0;
        Status = GetFormatCount(EncoderHandle, CodecGuid, &FormatCount);
        if (Status == NV_ENC_SUCCESS && FormatCount > 0)
        {
            TArray<NV_ENC_BUFFER_FORMAT> Formats;
            Formats.SetNumUninitialized(FormatCount);
            uint32 Returned = FormatCount;
            Status = GetFormats(EncoderHandle, CodecGuid, Formats.GetData(), FormatCount, &Returned);
            if (Status == NV_ENC_SUCCESS)
            {
                for (uint32 Index = 0; Index < Returned; ++Index)
                {
                    switch (Formats[Index])
                    {
                    case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
                        OutCapabilities.bSupports10Bit = true;
                        break;
                    case NV_ENC_BUFFER_FORMAT_YUV444:
                        OutCapabilities.bSupportsYUV444 = true;
                        break;
                    default:
                        break;
                    }
                }
            }
        }

        OutCapabilities.MaxWidth = QueryCap(EncoderHandle, Functions, CodecGuid, NV_ENC_CAPS_WIDTH_MAX);
        OutCapabilities.MaxHeight = QueryCap(EncoderHandle, Functions, CodecGuid, NV_ENC_CAPS_HEIGHT_MAX);
        OutCapabilities.bSupportsBFrames = QueryCap(EncoderHandle, Functions, CodecGuid, NV_ENC_CAPS_NUM_MAX_BFRAMES) > 0;
        OutCapabilities.bSupportsLookahead = QueryCap(EncoderHandle, Functions, CodecGuid, NV_ENC_CAPS_SUPPORT_LOOKAHEAD) != 0;
        OutCapabilities.bSupportsAdaptiveQuantization = QueryCap(EncoderHandle, Functions, CodecGuid, NV_ENC_CAPS_SUPPORT_TEMPORAL_AQ) != 0;

        if (!OutCapabilities.bSupports10Bit)
        {
            OutCapabilities.bSupports10Bit = QueryCap(EncoderHandle, Functions, CodecGuid, NV_ENC_CAPS_SUPPORT_10BIT_ENCODE) != 0;
        }

        if (!OutCapabilities.bSupportsYUV444)
        {
            OutCapabilities.bSupportsYUV444 = QueryCap(EncoderHandle, Functions, CodecGuid, NV_ENC_CAPS_SUPPORT_YUV444_ENCODE) != 0;
        }

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
