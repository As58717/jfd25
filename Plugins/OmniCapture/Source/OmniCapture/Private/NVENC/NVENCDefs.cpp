// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCDefs.h"

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCDefs, Log, All);

namespace OmniNVENC
{
    namespace
    {
        // These GUID values mirror the ones defined in the NVIDIA headers.  Having the
        // constants locally allows the trimmed down project to compile without pulling
        // in the proprietary SDK while still providing deterministic values for higher
        // level logic and unit tests.
        const FGuid& GuidFromComponents(uint32 A, uint32 B, uint32 C, uint32 D)
        {
            static TMap<uint64, FGuid> Cache;
            const uint64 Key = (static_cast<uint64>(A) << 32) | D;
            if (FGuid* Existing = Cache.Find(Key))
            {
                return *Existing;
            }

            FGuid Guid(A, B, C, D);
            Cache.Add(Key, Guid);
            return Cache[Key];
        }
    }

    const FGuid& FNVENCDefs::CodecGuid(ENVENCCodec Codec)
    {
        switch (Codec)
        {
        case ENVENCCodec::HEVC:
            // NV_ENC_CODEC_HEVC_GUID {0x790CDC65,0x7C5D,0x4FDE,{0x80,0x02,0x71,0xA5,0x15,0xC8,0x1A,0x6F}}
            return GuidFromComponents(0x790CDC65, 0x7C5D4FDE, 0x800271A5, 0x15C81A6F);
        case ENVENCCodec::H264:
        default:
            // NV_ENC_CODEC_H264_GUID {0x6BC82762,0x4E63,0x11D3,{0x9C,0xC1,0x00,0x80,0xC7,0xB3,0x12,0x97}}
            return GuidFromComponents(0x6BC82762, 0x4E6311D3, 0x9CC10080, 0xC7B31297);
        }
    }

    const FGuid& FNVENCDefs::PresetLowLatencyGuid()
    {
        // NV_ENC_PRESET_LOW_LATENCY_HQ_GUID {0xB3D9DC6F,0x9F9A,0x4FF2,{0xB2,0xEA,0xEF,0x0C,0xDE,0x24,0x82,0x5B}}
        return GuidFromComponents(0xB3D9DC6F, 0x9F9A4FF2, 0xB2EAEF0C, 0xDE24825B);
    }

    const FGuid& FNVENCDefs::PresetDefaultGuid()
    {
        // NV_ENC_PRESET_DEFAULT_GUID {0x60E4C05A,0x5333,0x4E09,{0x9A,0xB5,0x00,0xA3,0x1E,0x99,0x75,0x6F}}
        return GuidFromComponents(0x60E4C05A, 0x53334E09, 0x9AB500A3, 0x1E99756F);
    }

    const FGuid& FNVENCDefs::TuningLatencyGuid()
    {
        // NV_ENC_TUNING_INFO_LOW_LATENCY {0xD7363F6F,0x84F0,0x4176,{0xA0,0xE0,0x0D,0xA5,0x46,0x46,0x0B,0x7D}}
        return GuidFromComponents(0xD7363F6F, 0x84F04176, 0xA0E00DA5, 0x46460B7D);
    }

    const FGuid& FNVENCDefs::TuningQualityGuid()
    {
        // NV_ENC_TUNING_INFO_HIGH_QUALITY {0x1D69C67F,0x0F3C,0x4F25,{0x9F,0xA4,0xDF,0x7B,0xFB,0xB0,0x2E,0x59}}
        return GuidFromComponents(0x1D69C67F, 0x0F3C4F25, 0x9FA4DF7B, 0xFBB02E59);
    }

    FString FNVENCDefs::BufferFormatToString(ENVENCBufferFormat Format)
    {
        switch (Format)
        {
        case ENVENCBufferFormat::P010:
            return TEXT("P010");
        case ENVENCBufferFormat::BGRA:
            return TEXT("BGRA");
        case ENVENCBufferFormat::NV12:
        default:
            return TEXT("NV12");
        }
    }

    FString FNVENCDefs::CodecToString(ENVENCCodec Codec)
    {
        switch (Codec)
        {
        case ENVENCCodec::HEVC:
            return TEXT("HEVC");
        case ENVENCCodec::H264:
        default:
            return TEXT("H.264");
        }
    }

    FString FNVENCDefs::StatusToString(int32 StatusCode)
    {
        switch (StatusCode)
        {
        case 0:
            return TEXT("NV_ENC_SUCCESS");
        case 1:
            return TEXT("NV_ENC_ERR_NO_ENCODE_DEVICE");
        case 2:
            return TEXT("NV_ENC_ERR_UNSUPPORTED_DEVICE");
        case 3:
            return TEXT("NV_ENC_ERR_INVALID_ENCODERDEVICE");
        case 4:
            return TEXT("NV_ENC_ERR_INVALID_DEVICE");
        case 5:
            return TEXT("NV_ENC_ERR_DEVICE_NOT_EXIST");
        case 6:
            return TEXT("NV_ENC_ERR_INVALID_PTR");
        case 7:
            return TEXT("NV_ENC_ERR_INVALID_EVENT");
        case 8:
            return TEXT("NV_ENC_ERR_INVALID_PARAM");
        case 9:
            return TEXT("NV_ENC_ERR_INVALID_CALL");
        case 10:
            return TEXT("NV_ENC_ERR_OUT_OF_MEMORY");
        case 11:
            return TEXT("NV_ENC_ERR_ENCODER_NOT_INITIALIZED");
        case 12:
            return TEXT("NV_ENC_ERR_UNSUPPORTED_PARAM");
        case 13:
            return TEXT("NV_ENC_ERR_LOCK_BUSY");
        case 14:
            return TEXT("NV_ENC_ERR_NOT_ENOUGH_BUFFER");
        case 0x18:
            return TEXT("NV_ENC_ERR_NEED_MORE_INPUT");
        default:
            return FString::Printf(TEXT("NVENC_STATUS_%d"), StatusCode);
        }
    }

    uint32 FNVENCDefs::GetDefaultAPIVersion()
    {
        // We stick to a conservative default that matches the public SDK header.
        return 0x01010000u;
    }
}

