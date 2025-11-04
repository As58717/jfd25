// Copyright Epic Games, Inc. All Rights Reserved.

#include "Encoders/VideoEncoderNVENC.h"

#include "CodecPacket.h"
#include "Encoders/NVENCCommon.h"
#include "Encoders/NVENCDefs.h"
#include "Encoders/NVEncodeAPILoader.h"
#include "Encoders/NVENCCaps.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeExit.h"
#include "VideoEncoderFactory.h"
#include "Templates/RefCounting.h"

DEFINE_LOG_CATEGORY_STATIC(LogVideoEncoderNVENC, Log, All);

namespace AVEncoder
{
    namespace
    {
#if PLATFORM_WINDOWS
        template <typename TFunc>
        bool ValidateFunction(const ANSICHAR* Name, TFunc* Function)
        {
            if (!Function)
            {
                UE_LOG(LogVideoEncoderNVENC, Error, TEXT("Required NVENC export '%s' is missing."), ANSI_TO_TCHAR(Name));
                return false;
            }
            return true;
        }
#endif
    }

    void FVideoEncoderNVENC::Register(FVideoEncoderFactory& InFactory)
    {
        auto RegisterCodec = [&InFactory](ECodecType CodecType, const FString& CodecName)
        {
            FVideoEncoderInfo EncoderInfo;
            EncoderInfo.Name = FString::Printf(TEXT("NVIDIA NVENC %s"), *CodecName);
            EncoderInfo.Type = EVideoEncoderType::Hardware;
            EncoderInfo.CodecType = CodecType;
            EncoderInfo.SupportedRateControlModes = (1 << static_cast<uint32>(ERateControlMode::CBR)) | (1 << static_cast<uint32>(ERateControlMode::VBR)) | (1 << static_cast<uint32>(ERateControlMode::CONSTQP));
            EncoderInfo.SupportedFormats = (1 << static_cast<uint32>(EVideoFormat::NV12)) | (1 << static_cast<uint32>(EVideoFormat::P010)) | (1 << static_cast<uint32>(EVideoFormat::BGRA8));
            EncoderInfo.Capabilities = FVideoEncoderInfo::ECapabilities::SupportsDirectSubmission;

            InFactory.Register(EncoderInfo, []() { return MakeUnique<FVideoEncoderNVENC>(); });
        };

        RegisterCodec(ECodecType::H264, TEXT("H.264"));
        RegisterCodec(ECodecType::H265, TEXT("HEVC"));
    }

    FVideoEncoderNVENC::~FVideoEncoderNVENC()
    {
        Shutdown();
    }

    bool FVideoEncoderNVENC::Setup(TSharedRef<FVideoEncoderInput> InInput, const FLayerConfig& InLayerConfig)
    {
#if !PLATFORM_WINDOWS
        UE_LOG(LogVideoEncoderNVENC, Error, TEXT("NVENC encoder is only available on Windows."));
        return false;
#else
        if (!FNVEncodeAPILoader::Get().Load())
        {
            UE_LOG(LogVideoEncoderNVENC, Error, TEXT("Failed to load NVENC runtime – encoder is unavailable."));
            return false;
        }

        TRefCountPtr<ID3D11Device> InputDevice = InInput->GetD3D11EncoderDevice();
        if (!InputDevice)
        {
            UE_LOG(LogVideoEncoderNVENC, Error, TEXT("NVENC requires a D3D11 encoder device."));
            return false;
        }

        Session = MakeUnique<FNVENCSession>();
        if (!Session->Open(ENVENCCodec::H264, InputDevice.GetReference(), NV_ENC_DEVICE_TYPE_DIRECTX))
        {
            UE_LOG(LogVideoEncoderNVENC, Error, TEXT("Failed to open NVENC session."));
            Shutdown();
            return false;
        }

        CachedParameters = FNVENCParameterMapper::FromLayerConfig(InLayerConfig, ENVENCCodec::H264, ENVENCBufferFormat::BGRA);
        if (!Session->Initialize(CachedParameters))
        {
            UE_LOG(LogVideoEncoderNVENC, Error, TEXT("Failed to initialise NVENC session."));
            Shutdown();
            return false;
        }

        if (!D3D11Input.Initialise(InputDevice.GetReference(), *Session))
        {
            UE_LOG(LogVideoEncoderNVENC, Error, TEXT("Failed to initialise NVENC D3D11 input bridge."));
            Shutdown();
            return false;
        }

        if (!Bitstream.Initialize(Session->GetEncoderHandle(), Session->GetFunctionList()))
        {
            UE_LOG(LogVideoEncoderNVENC, Error, TEXT("Failed to allocate NVENC bitstream buffer."));
            Shutdown();
            return false;
        }

        AcquireSequenceParameters();

        EncoderDevice = InputDevice;
        SourceInput = InInput;
        bIsReady = true;
        return FVideoEncoder::Setup(InInput, InLayerConfig);
#endif
    }

    bool FVideoEncoderNVENC::AcquireSequenceParameters()
    {
#if !PLATFORM_WINDOWS
        return false;
#else
        if (!Session)
        {
            return false;
        }

        using TNvEncGetSequenceParams = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_SEQUENCE_PARAM_PAYLOAD*);
        TNvEncGetSequenceParams SequenceParamsFn = Session->GetFunctionList().nvEncGetSequenceParams;
        if (!SequenceParamsFn)
        {
            return false;
        }

        TArray<uint8> Payload;
        Payload.SetNumZeroed(1024);

        NV_ENC_SEQUENCE_PARAM_PAYLOAD PayloadParams = {};
        PayloadParams.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
        PayloadParams.inBufferSize = Payload.Num();
        PayloadParams.spsppsBuffer = Payload.GetData();
        PayloadParams.vpsBuffer = Payload.GetData();

        NVENCSTATUS Status = SequenceParamsFn(Session->GetEncoderHandle(), &PayloadParams);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogVideoEncoderNVENC, Verbose, TEXT("NvEncGetSequenceParams returned %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        const uint32 TotalPayload = PayloadParams.outSPSPPSPayloadSize + PayloadParams.outVPSPayloadSize;
        if (TotalPayload == 0)
        {
            return false;
        }

        Payload.SetNumUninitialized(TotalPayload);
        AnnexB.SetCodecConfig(Payload);
        return true;
#endif
    }

    void FVideoEncoderNVENC::Encode(const FVideoEncoderInputFrame* InFrame, const FEncodeOptions& InOptions)
    {
        if (!bIsReady || !Session || !Session->IsInitialised())
        {
            if (InFrame)
            {
                InFrame->Release();
            }
            return;
        }

        if (!InFrame)
        {
            return;
        }

#if PLATFORM_WINDOWS
        if (!EncodeFrameD3D11(InFrame, InOptions))
        {
            InFrame->Release();
        }
#else
        InFrame->Release();
#endif
    }

    bool FVideoEncoderNVENC::EncodeFrameD3D11(const FVideoEncoderInputFrame* InFrame, const FEncodeOptions& InOptions)
    {
#if !PLATFORM_WINDOWS
        return false;
#else
        const FVideoEncoderInputFrame::FD3D11& D3DInfo = InFrame->GetD3D11();
        ID3D11Texture2D* Texture = D3DInfo.EncoderTexture;
        if (!Texture)
        {
            UE_LOG(LogVideoEncoderNVENC, Error, TEXT("NVENC encode failed – input frame did not provide a D3D11 texture."));
            return false;
        }

        if (!D3D11Input.RegisterResource(Texture))
        {
            UE_LOG(LogVideoEncoderNVENC, Error, TEXT("Failed to register D3D11 texture with NVENC."));
            return false;
        }

        NV_ENC_INPUT_PTR InputPtr = nullptr;
        if (!D3D11Input.MapResource(Texture, InputPtr))
        {
            UE_LOG(LogVideoEncoderNVENC, Error, TEXT("Failed to map D3D11 texture for NVENC."));
            return false;
        }

        ON_SCOPE_EXIT
        {
            D3D11Input.UnmapResource(InputPtr);
        };

        using TNvEncEncodePicture = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_PIC_PARAMS*);
        TNvEncEncodePicture EncodePictureFn = Session->GetFunctionList().nvEncEncodePicture;
        if (!ValidateFunction("NvEncEncodePicture", EncodePictureFn))
        {
            return false;
        }

        NV_ENC_PIC_PARAMS PicParams = {};
        PicParams.version = NV_ENC_PIC_PARAMS_VER;
        PicParams.inputBuffer = InputPtr;
        PicParams.bufferFmt = Session->GetNVBufferFormat();
        PicParams.inputWidth = CachedParameters.Width;
        PicParams.inputHeight = CachedParameters.Height;
        PicParams.outputBitstream = Bitstream.GetBitstreamBuffer();
        PicParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        PicParams.inputTimeStamp = static_cast<uint64>(InFrame->GetTimestampUs());
        PicParams.encodePicFlags = InOptions.bForceKeyFrame ? NV_ENC_PIC_FLAG_FORCEIDR : 0;

        NVENCSTATUS Status = EncodePictureFn(Session->GetEncoderHandle(), &PicParams);
        if (Status != NV_ENC_SUCCESS && Status != NV_ENC_ERR_NEED_MORE_INPUT)
        {
            UE_LOG(LogVideoEncoderNVENC, Error, TEXT("NvEncEncodePicture failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        void* BitstreamBuffer = nullptr;
        int32 BitstreamSize = 0;
        if (!Bitstream.Lock(BitstreamBuffer, BitstreamSize))
        {
            UE_LOG(LogVideoEncoderNVENC, Error, TEXT("Failed to lock NVENC bitstream."));
            return false;
        }

        UE_LOG(LogVideoEncoderNVENC, VeryVerbose, TEXT("NVENC bitstream locked with %d bytes ready."), BitstreamSize);

        ON_SCOPE_EXIT
        {
            Bitstream.Unlock();
        };

        FNVENCEncodedPacket EncodedPacket;
        if (!Bitstream.ExtractPacket(EncodedPacket) || EncodedPacket.Data.Num() == 0)
        {
            InFrame->Release();
            return true;
        }

        if (EncodedPacket.bKeyFrame)
        {
            const TArray<uint8>& Config = AnnexB.GetCodecConfig();
            if (Config.Num() > 0)
            {
                const int32 OriginalNum = EncodedPacket.Data.Num();
                EncodedPacket.Data.Insert(Config.GetData(), Config.Num(), 0);
                UE_LOG(LogVideoEncoderNVENC, Verbose, TEXT("Prepended %d bytes of Annex B configuration to key frame payload (original %d bytes)."), Config.Num(), OriginalNum);
            }
        }

        FCodecPacket Packet = FCodecPacket::Create(EncodedPacket.Data.GetData(), EncodedPacket.Data.Num());
        Packet.IsKeyFrame = EncodedPacket.bKeyFrame;
        Packet.Framerate = CachedParameters.Framerate;
        Packet.Timings.StartTs = FTimespan::FromMicroseconds(InFrame->GetTimestampUs());
        Packet.Timings.FinishTs = Packet.Timings.StartTs;

        TSharedPtr<FVideoEncoderInputFrame> FrameShared(const_cast<FVideoEncoderInputFrame*>(InFrame), [](FVideoEncoderInputFrame* Frame)
        {
            Frame->Release();
        });

        if (OnEncodedPacket)
        {
            OnEncodedPacket(0, FrameShared, Packet);
            FrameShared.Reset();
        }
        else
        {
            FrameShared.Reset();
        }

        return true;
#endif
    }

    void FVideoEncoderNVENC::Flush()
    {
        if (Session)
        {
            Session->Flush();
        }
    }

    void FVideoEncoderNVENC::Shutdown()
    {
        Bitstream.Release();
        D3D11Input.Shutdown();

        if (Session)
        {
            Session->Destroy();
            Session.Reset();
        }

        AnnexB.Reset();
        EncoderDevice = nullptr;
        SourceInput.Reset();
        bIsReady = false;
    }
}

