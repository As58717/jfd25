// Copyright Epic Games, Inc. All Rights Reserved.

#include "Encoders/NVENCCommon.h"

#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCCommon, Log, All);

namespace AVEncoder
{
    namespace
    {
        struct FNVENCLoader
        {
            void* ModuleHandle = nullptr;
        };

        FNVENCLoader& GetLoader()
        {
            static FNVENCLoader Loader;
            return Loader;
        }
    }

    bool FNVENCCommon::EnsureLoaded()
    {
        FScopeLock Lock(&GetMutex());
        FNVENCLoader& Loader = GetLoader();
        if (Loader.ModuleHandle)
        {
            return true;
        }

#if PLATFORM_WINDOWS
        const FString DllPath = GetDefaultDllName();
        Loader.ModuleHandle = FPlatformProcess::GetDllHandle(*DllPath);
        if (!Loader.ModuleHandle)
        {
            UE_LOG(LogNVENCCommon, Warning, TEXT("Unable to load NVENC runtime module '%s'."), *DllPath);
            return false;
        }
        return true;
#else
        UE_LOG(LogNVENCCommon, Warning, TEXT("NVENC runtime loading only implemented on Windows."));
        return false;
#endif
    }

    void* FNVENCCommon::GetHandle()
    {
        FScopeLock Lock(&GetMutex());
        return GetLoader().ModuleHandle;
    }

    void FNVENCCommon::Shutdown()
    {
        FScopeLock Lock(&GetMutex());
        FNVENCLoader& Loader = GetLoader();
        if (Loader.ModuleHandle)
        {
            FPlatformProcess::FreeDllHandle(Loader.ModuleHandle);
            Loader.ModuleHandle = nullptr;
        }
    }

    FString FNVENCCommon::GetDefaultDllName()
    {
#if PLATFORM_WINDOWS
#if PLATFORM_64BITS
        return TEXT("nvEncodeAPI64.dll");
#else
        return TEXT("nvEncodeAPI.dll");
#endif
#else
        return FString();
#endif
    }

    FCriticalSection& FNVENCCommon::GetMutex()
    {
        static FCriticalSection Mutex;
        return Mutex;
    }
}

