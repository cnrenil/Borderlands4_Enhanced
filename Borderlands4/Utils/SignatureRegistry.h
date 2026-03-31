#pragma once

#include <string>
#include <vector>

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
        bool bResolveRipRelative = false;
        size_t RipDisplacementOffset = 3;
        size_t RipInstructionLength = 7;
    };

    struct SignatureSnapshot
    {
        std::string Name;
        HookTiming Timing = HookTiming::Immediate;
        uintptr_t CachedAddress = 0;
        bool bResolveFailed = false;
        bool bHookInstalled = false;
        bool bTimingReady = false;
    };

    void Clear();
    void Register(
        const char* name,
        const char* pattern,
        HookTiming timing = HookTiming::Immediate,
        bool bResolveRipRelative = false,
        size_t ripDisplacementOffset = 3,
        size_t ripInstructionLength = 7);
    bool IsTimingReady(HookTiming timing);
    bool ShouldAttempt(const char* name, HookTiming timing);
    uintptr_t Resolve(const char* name);
    uintptr_t Resolve(const Signature& signature);
    std::vector<SignatureSnapshot> GetSnapshots();
    bool EnsureHook(
        const Signature& signature,
        void* detour,
        void** originalOut,
        size_t fallbackLen,
        const char* logCategory,
        bool bAllowAbsoluteFallback = true);
}
