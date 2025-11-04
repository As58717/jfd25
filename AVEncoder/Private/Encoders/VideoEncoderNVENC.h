// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VideoEncoder.h"

#include "Encoders/NVENCAnnexB.h"
#include "Encoders/NVENCBitstream.h"
#include "Encoders/NVENCInputD3D11.h"
#include "Encoders/NVENCParameters.h"
#include "Encoders/NVENCSession.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
struct ID3D11Device;
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace AVEncoder
{
    /**
     * Placeholder NVENC encoder implementation.
     *
     * The project this repository was extracted from stripped the production ready encoder.  In
     * order to make the OmniCapture plugin able to instantiate an NVENC encoder we provide a light
     * weight implementation that focuses on runtime validation and plumbing.  Actual encoding is
     * intentionally left unimplemented – calling Encode will result in a descriptive warning so
     * the caller can gracefully fall back to a different encoder.
     */
    class FVideoEncoderNVENC : public FVideoEncoder
    {
    public:
        static void Register(FVideoEncoderFactory& InFactory);

        virtual ~FVideoEncoderNVENC() override;

        // FVideoEncoder implementation
        virtual bool Setup(TSharedRef<FVideoEncoderInput> InInput, const FLayerConfig& InLayerConfig) override;
        virtual void Encode(const FVideoEncoderInputFrame* InFrame, const FEncodeOptions& InOptions) override;
        virtual void Flush() override;
        virtual void Shutdown() override;

    private:
        bool EncodeFrameD3D11(const FVideoEncoderInputFrame* InFrame, const FEncodeOptions& InOptions);
        bool AcquireSequenceParameters();

        bool bIsReady = false;
        TUniquePtr<FNVENCSession> Session;
        FNVENCParameters CachedParameters;
        FNVENCAnnexB AnnexB;
        FNVENCBitstream Bitstream;
        FNVENCInputD3D11 D3D11Input;
        TWeakPtr<FVideoEncoderInput> SourceInput;
#if PLATFORM_WINDOWS
        TRefCountPtr<ID3D11Device> EncoderDevice;
#endif
    };
}

