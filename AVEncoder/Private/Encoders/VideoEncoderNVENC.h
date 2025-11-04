// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VideoEncoder.h"

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
        bool bIsReady = false;
    };
}

