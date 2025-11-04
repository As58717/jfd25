// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace AVEncoder
{
    /**
     * Minimal helper responsible for loading the NVENC runtime module on demand.
     *
     * The original UE implementation ships a much more feature rich wrapper that also exposes
     * capabilities and maintains shared state between the different encoder instances.  For the
     * trimmed down encoder that exists in this repository we only need a centralised place to
     * lazily load and unload the dll.  The rest of the encoder implementation only interacts with
     * the handle that is exposed here.
     */
    class FNVENCCommon
    {
    public:
        /** Attempt to load the NVENC runtime. */
        static bool EnsureLoaded();

        /** Return the dll handle if it was successfully loaded. */
        static void* GetHandle();

        /** Unload the runtime when the module shuts down. */
        static void Shutdown();

    private:
        static FString GetDefaultDllName();
        static FCriticalSection& GetMutex();
    };
}

