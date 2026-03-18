#pragma once

#include <string>

namespace SignatureRegistry
{
    enum class HookTiming
    {
        Immediate,
        InGameReady,
    };

    struct Signature
    {
        const char* Name;
        const char* Pattern;
        HookTiming Timing = HookTiming::Immediate;
    };

    void Clear();
    void Register(const char* name, const char* pattern, HookTiming timing = HookTiming::Immediate);
    bool ShouldAttempt(const char* name, HookTiming timing);
    uintptr_t Resolve(const char* name);
    uintptr_t Resolve(const Signature& signature);
    bool EnsureHook(
        const Signature& signature,
        void* detour,
        void** originalOut,
        size_t fallbackLen,
        const char* logCategory);
}
