// Copyright Epic Games, Inc. All Rights Reserved.

#include "Encoders/NVEncodeAPILoader.h"

#include "Encoders/NVENCCommon.h"
#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVEncodeAPILoader, Log, All);

namespace AVEncoder
{
    namespace
    {
        struct FFunctionLookup
        {
            const ANSICHAR* Name;
            void** Target;
        };

        template <int32 N>
        bool ResolveFunctions(void* LibraryHandle, const FFunctionLookup (&Entries)[N])
        {
            if (!LibraryHandle)
            {
                return false;
            }

            for (const FFunctionLookup& Entry : Entries)
            {
                if (!Entry.Target)
                {
                    continue;
                }

                *Entry.Target = FPlatformProcess::GetDllExport(LibraryHandle, Entry.Name);
                if (!*Entry.Target)
                {
                    UE_LOG(LogNVEncodeAPILoader, Verbose, TEXT("Failed to resolve NVENC export '%s'."), ANSI_TO_TCHAR(Entry.Name));
                    return false;
                }
            }

            return true;
        }
    }

    FNVEncodeAPILoader& FNVEncodeAPILoader::Get()
    {
        static FNVEncodeAPILoader Instance;
        return Instance;
    }

    bool FNVEncodeAPILoader::Load()
    {
        if (bLoaded)
        {
            return true;
        }

        if (bAttemptedLoad && !bLoaded)
        {
            return false;
        }

        bAttemptedLoad = true;

        if (!FNVENCCommon::EnsureLoaded())
        {
            UE_LOG(LogNVEncodeAPILoader, Warning, TEXT("Failed to load NVENC runtime module."));
            Reset();
            return false;
        }

        void* const LibraryHandle = FNVENCCommon::GetHandle();
        if (!LibraryHandle)
        {
            UE_LOG(LogNVEncodeAPILoader, Warning, TEXT("NVENC module handle was null."));
            Reset();
            return false;
        }

        const FFunctionLookup Lookups[] = {
            { "NvEncodeAPICreateInstance", &Functions.NvEncodeAPICreateInstance },
            { "NvEncOpenEncodeSessionEx", &Functions.NvEncOpenEncodeSessionEx },
            { "NvEncInitializeEncoder", &Functions.NvEncInitializeEncoder },
            { "NvEncReconfigureEncoder", &Functions.NvEncReconfigureEncoder },
            { "NvEncEncodePicture", &Functions.NvEncEncodePicture },
            { "NvEncDestroyEncoder", &Functions.NvEncDestroyEncoder },
            { "NvEncFlushEncoderQueue", &Functions.NvEncFlushEncoderQueue },
            { "NvEncGetEncodeCaps", &Functions.NvEncGetEncodeCaps },
            { "NvEncGetEncodePresetGUIDs", &Functions.NvEncGetEncodePresetGUIDs },
            { "NvEncGetEncodeProfileGUIDs", &Functions.NvEncGetEncodeProfileGUIDs },
            { "NvEncGetEncodePresetConfig", &Functions.NvEncGetEncodePresetConfig },
            { "NvEncCreateInputBuffer", &Functions.NvEncCreateInputBuffer },
            { "NvEncDestroyInputBuffer", &Functions.NvEncDestroyInputBuffer },
            { "NvEncCreateBitstreamBuffer", &Functions.NvEncCreateBitstreamBuffer },
            { "NvEncDestroyBitstreamBuffer", &Functions.NvEncDestroyBitstreamBuffer },
            { "NvEncRegisterResource", &Functions.NvEncRegisterResource },
            { "NvEncUnregisterResource", &Functions.NvEncUnregisterResource },
            { "NvEncMapInputResource", &Functions.NvEncMapInputResource },
            { "NvEncUnmapInputResource", &Functions.NvEncUnmapInputResource },
            { "NvEncLockInputBuffer", &Functions.NvEncLockInputBuffer },
            { "NvEncUnlockInputBuffer", &Functions.NvEncUnlockInputBuffer },
            { "NvEncLockBitstream", &Functions.NvEncLockBitstream },
            { "NvEncUnlockBitstream", &Functions.NvEncUnlockBitstream },
            { "NvEncGetSequenceParams", &Functions.NvEncGetSequenceParams },
        };

        if (!ResolveFunctions(LibraryHandle, Lookups))
        {
            UE_LOG(LogNVEncodeAPILoader, Warning, TEXT("NVENC runtime is missing required exports."));
            Reset();
            return false;
        }

        bLoaded = true;
        return true;
    }

    void FNVEncodeAPILoader::Unload()
    {
        Reset();
        FNVENCCommon::Shutdown();
    }

    void* FNVEncodeAPILoader::GetFunction(const ANSICHAR* FunctionName) const
    {
        if (!FunctionName)
        {
            return nullptr;
        }

        const FFunctionLookup Lookups[] = {
            { "NvEncodeAPICreateInstance", const_cast<void**>(&Functions.NvEncodeAPICreateInstance) },
            { "NvEncOpenEncodeSessionEx", const_cast<void**>(&Functions.NvEncOpenEncodeSessionEx) },
            { "NvEncInitializeEncoder", const_cast<void**>(&Functions.NvEncInitializeEncoder) },
            { "NvEncReconfigureEncoder", const_cast<void**>(&Functions.NvEncReconfigureEncoder) },
            { "NvEncEncodePicture", const_cast<void**>(&Functions.NvEncEncodePicture) },
            { "NvEncDestroyEncoder", const_cast<void**>(&Functions.NvEncDestroyEncoder) },
            { "NvEncFlushEncoderQueue", const_cast<void**>(&Functions.NvEncFlushEncoderQueue) },
            { "NvEncGetEncodeCaps", const_cast<void**>(&Functions.NvEncGetEncodeCaps) },
            { "NvEncGetEncodePresetGUIDs", const_cast<void**>(&Functions.NvEncGetEncodePresetGUIDs) },
            { "NvEncGetEncodeProfileGUIDs", const_cast<void**>(&Functions.NvEncGetEncodeProfileGUIDs) },
            { "NvEncGetEncodePresetConfig", const_cast<void**>(&Functions.NvEncGetEncodePresetConfig) },
            { "NvEncCreateInputBuffer", const_cast<void**>(&Functions.NvEncCreateInputBuffer) },
            { "NvEncDestroyInputBuffer", const_cast<void**>(&Functions.NvEncDestroyInputBuffer) },
            { "NvEncCreateBitstreamBuffer", const_cast<void**>(&Functions.NvEncCreateBitstreamBuffer) },
            { "NvEncDestroyBitstreamBuffer", const_cast<void**>(&Functions.NvEncDestroyBitstreamBuffer) },
            { "NvEncRegisterResource", const_cast<void**>(&Functions.NvEncRegisterResource) },
            { "NvEncUnregisterResource", const_cast<void**>(&Functions.NvEncUnregisterResource) },
            { "NvEncMapInputResource", const_cast<void**>(&Functions.NvEncMapInputResource) },
            { "NvEncUnmapInputResource", const_cast<void**>(&Functions.NvEncUnmapInputResource) },
            { "NvEncLockInputBuffer", const_cast<void**>(&Functions.NvEncLockInputBuffer) },
            { "NvEncUnlockInputBuffer", const_cast<void**>(&Functions.NvEncUnlockInputBuffer) },
            { "NvEncLockBitstream", const_cast<void**>(&Functions.NvEncLockBitstream) },
            { "NvEncUnlockBitstream", const_cast<void**>(&Functions.NvEncUnlockBitstream) },
            { "NvEncGetSequenceParams", const_cast<void**>(&Functions.NvEncGetSequenceParams) },
        };

        for (const FFunctionLookup& Lookup : Lookups)
        {
            if (FCStringAnsi::Stricmp(FunctionName, Lookup.Name) == 0)
            {
                return Lookup.Target ? *Lookup.Target : nullptr;
            }
        }

        return nullptr;
    }

    void FNVEncodeAPILoader::Reset()
    {
        Functions = FFunctions();
        bLoaded = false;
    }
}

