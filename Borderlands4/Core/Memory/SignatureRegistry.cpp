#include "pch.h"

#include <unordered_map>

namespace SignatureRegistry
{
    namespace
    {
        constexpr size_t kAbsoluteFallbackMaxDecodeLen = 64;

        struct SignatureEntry
        {
            std::string Pattern;
            HookTiming Timing = HookTiming::Immediate;
            uintptr_t CachedAddress = 0;
            bool bResolveRipRelative = false;
            size_t RipDisplacementOffset = 3;
            size_t RipInstructionLength = 7;
            bool bResolveFailed = false;
            bool bHookInstalled = false;
            ULONGLONG LastAttemptMs = 0;
            int ConsecutiveInstallFailures = 0;
        };

        std::unordered_map<std::string, SignatureEntry> g_Signatures;

        uintptr_t g_LastInGameWorld = 0;
        ULONGLONG g_InGameReadySinceMs = 0;
        // Delay signature-backed gameplay hooks a little longer after map entry.
        constexpr uint64_t kInGameReadyWarmupMs = 4000;
        constexpr ULONGLONG kInGameRetryIntervalMs = 5000;

        bool IsCommittedAddress(uintptr_t address, size_t size = sizeof(void*))
        {
            if (!address)
                return false;

            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0)
                return false;
            if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
                return false;

            const uintptr_t regionStart = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            const uintptr_t regionEnd = regionStart + mbi.RegionSize;
            return address >= regionStart && (address + size) <= regionEnd;
        }

        uintptr_t ResolveRipRelativeAddress(uintptr_t instructionAddress, size_t displacementOffset, size_t instructionLength)
        {
            if (!instructionAddress)
                return 0;

            __try
            {
                const int32 displacement = *reinterpret_cast<int32*>(instructionAddress + displacementOffset);
                return instructionAddress + instructionLength + static_cast<intptr_t>(displacement);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return 0;
            }
        }

        bool TryInstallAbsoluteFallback(
            SignatureEntry& entry,
            const Signature& signature,
            void* target,
            void* detour,
            void** originalOut,
            size_t fallbackLen,
            const char* logCategory,
            uintptr_t targetAddress,
            int failureCount)
        {
            if (fallbackLen == 0)
                return false;

            const size_t safeHookLen = Memory::CalculateSafeHookLength(
                target,
                fallbackLen,
                kAbsoluteFallbackMaxDecodeLen);
            if (safeHookLen == 0)
            {
                LOG_WARN(
                    logCategory ? logCategory : "Signature",
                    "Absolute detour fallback rejected for '%s': could not decode a safe hook length (min=%zu, max=%zu) at 0x%llX",
                    signature.Name,
                    fallbackLen,
                    kAbsoluteFallbackMaxDecodeLen,
                    static_cast<unsigned long long>(targetAddress));
                return false;
            }

            if (Memory::HookFunctionAbsolute(target, detour, originalOut, safeHookLen))
            {
                entry.bHookInstalled = true;
                entry.ConsecutiveInstallFailures = 0;
                LOG_DEBUG(
                    logCategory ? logCategory : "Signature",
                    "SUCCESS: '%s' hook installed via absolute detour fallback after %d MinHook failures at 0x%llX (stolenLen=%zu)",
                    signature.Name,
                    failureCount,
                    static_cast<unsigned long long>(targetAddress),
                    safeHookLen);
                return true;
            }

            LOG_WARN(
                logCategory ? logCategory : "Signature",
                "Absolute detour fallback failed for '%s' at 0x%llX (stolenLen=%zu)",
                signature.Name,
                static_cast<unsigned long long>(targetAddress),
                safeHookLen);
            return false;
        }

        bool IsHookTimingReady(HookTiming timing)
        {
            if (timing == HookTiming::Immediate)
                return true;

            const bool gameplayReady = Core::Scheduler::State().CanRunGameplay();
            if (!gameplayReady ||
                !GVars.World ||
                !GVars.Character ||
                !GVars.PlayerController ||
                !GVars.PlayerController->PlayerCameraManager)
            {
                g_LastInGameWorld = 0;
                g_InGameReadySinceMs = 0;
                // Log throttled to avoid flooding the log
                Logger::LogThrottled(Logger::Level::Debug, "Signature", 5000, "Global hooks timing gate not ready (GameplayReady: %d, World: %p, PC: %p)",
                    gameplayReady ? 1 : 0, GVars.World, GVars.PlayerController);
                return false;
            }

            const uintptr_t currentWorld = reinterpret_cast<uintptr_t>(GVars.World);
            const ULONGLONG nowMs = GetTickCount64();
            if (g_LastInGameWorld != currentWorld)
            {
                g_LastInGameWorld = currentWorld;
                g_InGameReadySinceMs = nowMs;
                return false;
            }

            if (g_InGameReadySinceMs == 0)
            {
                g_InGameReadySinceMs = nowMs;
                return false;
            }

            const bool ready = (nowMs - g_InGameReadySinceMs) >= kInGameReadyWarmupMs;
            if (!ready)
            {
                Logger::LogThrottled(Logger::Level::Debug, "Signature", 5000, "Hook timing warmup in progress (%llu ms remaining)", 
                    kInGameReadyWarmupMs - (nowMs - g_InGameReadySinceMs));
            }
            return ready;
        }

        ULONGLONG GetRetryIntervalMs(HookTiming timing)
        {
            return timing == HookTiming::InGameReady ? kInGameRetryIntervalMs : 0;
        }
    }

    void Clear()
    {
        g_Signatures.clear();
        g_LastInGameWorld = 0;
        g_InGameReadySinceMs = 0;
    }

    void Register(
        const char* name,
        const char* pattern,
        HookTiming timing,
        bool bResolveRipRelative,
        size_t ripDisplacementOffset,
        size_t ripInstructionLength)
    {
        if (!name || !pattern || name[0] == '\0' || pattern[0] == '\0')
            return;

        auto& entry = g_Signatures[std::string(name)];
        const bool bPatternChanged = entry.Pattern != pattern;
        const bool bTimingChanged = entry.Timing != timing;
        const bool bRipModeChanged = entry.bResolveRipRelative != bResolveRipRelative;
        const bool bRipLayoutChanged =
            entry.RipDisplacementOffset != ripDisplacementOffset ||
            entry.RipInstructionLength != ripInstructionLength;
        const bool bWasNewEntry = entry.Pattern.empty();
        entry.Pattern = pattern;
        entry.Timing = timing;
        entry.bResolveRipRelative = bResolveRipRelative;
        entry.RipDisplacementOffset = ripDisplacementOffset;
        entry.RipInstructionLength = ripInstructionLength;
        if (bPatternChanged)
        {
            entry.CachedAddress = 0;
            entry.bResolveFailed = false;
            entry.bHookInstalled = false;
            entry.ConsecutiveInstallFailures = 0;
        }
        if (bPatternChanged || bTimingChanged || bRipModeChanged || bRipLayoutChanged)
        {
            entry.CachedAddress = 0;
            entry.bResolveFailed = false;
            entry.LastAttemptMs = 0;
        }

        if (bWasNewEntry)
        {
            LOG_DEBUG(
                "Signature",
                "Registered '%s' (timing=%d, ripRelative=%d, disp=%zu, len=%zu).",
                name,
                static_cast<int>(timing),
                bResolveRipRelative ? 1 : 0,
                ripDisplacementOffset,
                ripInstructionLength);
        }
        else if (bPatternChanged || bTimingChanged || bRipModeChanged || bRipLayoutChanged)
        {
            LOG_DEBUG(
                "Signature",
                "Updated '%s' registration (patternChanged=%d, timingChanged=%d, ripModeChanged=%d, ripLayoutChanged=%d, timing=%d, ripRelative=%d, disp=%zu, len=%zu).",
                name,
                bPatternChanged ? 1 : 0,
                bTimingChanged ? 1 : 0,
                bRipModeChanged ? 1 : 0,
                bRipLayoutChanged ? 1 : 0,
                static_cast<int>(timing),
                bResolveRipRelative ? 1 : 0,
                ripDisplacementOffset,
                ripInstructionLength);
        }
    }

    bool IsTimingReady(HookTiming timing)
    {
        return IsHookTimingReady(timing);
    }

    bool ShouldAttempt(const char* name, HookTiming timing)
    {
        if (!name || name[0] == '\0')
            return false;

        if (!IsHookTimingReady(timing))
            return false;

        auto it = g_Signatures.find(std::string(name));
        if (it == g_Signatures.end())
            return false;

        auto& entry = it->second;
        entry.Timing = timing;

        const ULONGLONG retryIntervalMs = GetRetryIntervalMs(timing);
        const ULONGLONG nowMs = GetTickCount64();
        if (retryIntervalMs != 0 &&
            entry.LastAttemptMs != 0 &&
            (nowMs - entry.LastAttemptMs) < retryIntervalMs)
        {
            Logger::LogThrottled(
                Logger::Level::Debug,
                "Signature",
                5000,
                "Skipping '%s': retry interval not reached (%llums remaining).",
                name,
                static_cast<unsigned long long>(retryIntervalMs - (nowMs - entry.LastAttemptMs)));
            return false;
        }

        entry.LastAttemptMs = nowMs;
        return true;
    }

    uintptr_t Resolve(const char* name)
    {
        if (!name || name[0] == '\0')
            return 0;

        auto it = g_Signatures.find(std::string(name));
        if (it == g_Signatures.end())
        {
            LOG_WARN("Signature", "Resolve called for unregistered signature '%s'.", name);
            return 0;
        }

        auto& entry = it->second;
        if (!IsTimingReady(entry.Timing))
        {
            return 0;
        }

        if (!ShouldAttempt(name, entry.Timing))
            return 0;

        if (entry.bResolveFailed)
        {
            LOG_DEBUG("Signature", "Skipping '%s': resolve previously failed.", name);
            return 0;
        }

        if (entry.CachedAddress)
        {
            LOG_DEBUG(
                "Signature",
                "Using cached address for '%s': 0x%llX",
                name,
                static_cast<unsigned long long>(entry.CachedAddress));
            return entry.CachedAddress;
        }

        LOG_DEBUG("Signature", "Scanning '%s' with pattern: %s", name, entry.Pattern.c_str());
        const uintptr_t rawAddress = Memory::FindPattern(entry.Pattern.c_str());
        if (!rawAddress)
        {
            entry.bResolveFailed = true;
            LOG_WARN("Signature", "Pattern not found for '%s'.", name);
            return 0;
        }

        const uintptr_t resolvedAddress = entry.bResolveRipRelative
            ? ResolveRipRelativeAddress(rawAddress, entry.RipDisplacementOffset, entry.RipInstructionLength)
            : rawAddress;
        if (!IsCommittedAddress(resolvedAddress))
        {
            entry.bResolveFailed = true;
            LOG_WARN(
                "Signature",
                "Resolved address invalid for '%s': raw=0x%llX, resolved=0x%llX, ripRelative=%d",
                name,
                static_cast<unsigned long long>(rawAddress),
                static_cast<unsigned long long>(resolvedAddress),
                entry.bResolveRipRelative ? 1 : 0);
            return 0;
        }

        entry.CachedAddress = resolvedAddress;
        LOG_DEBUG(
            "Signature",
            "Resolved '%s': raw=0x%llX, final=0x%llX, ripRelative=%d",
            name,
            static_cast<unsigned long long>(rawAddress),
            static_cast<unsigned long long>(entry.CachedAddress),
            entry.bResolveRipRelative ? 1 : 0);
        return entry.CachedAddress;
    }

    uintptr_t Resolve(const Signature& signature)
    {
        Register(
            signature.Name,
            signature.Pattern,
            signature.Timing,
            signature.bResolveRipRelative,
            signature.RipDisplacementOffset,
            signature.RipInstructionLength);
        return Resolve(signature.Name);
    }

    std::vector<SignatureSnapshot> GetSnapshots()
    {
        std::vector<SignatureSnapshot> snapshots;
        snapshots.reserve(g_Signatures.size());

        for (const auto& [name, entry] : g_Signatures)
        {
            snapshots.push_back(SignatureSnapshot{
                name,
                entry.Timing,
                entry.CachedAddress,
                entry.bResolveFailed,
                entry.bHookInstalled,
                IsHookTimingReady(entry.Timing)
            });
        }

        std::sort(snapshots.begin(), snapshots.end(), [](const SignatureSnapshot& a, const SignatureSnapshot& b)
        {
            if (a.bHookInstalled != b.bHookInstalled)
                return a.bHookInstalled > b.bHookInstalled;
            return a.Name < b.Name;
        });

        return snapshots;
    }

    bool EnsureHook(const Signature& signature, void* detour, void** originalOut, bool bStealth)
    {
        // 19 bytes is a safe default for almost any instruction sequence we hook
        return EnsureHook(signature, detour, originalOut, 19, signature.Name, true, bStealth);
    }

    bool EnsureHook(
        const Signature& signature,
        void* detour,
        void** originalOut,
        size_t fallbackLen,
        const char* logCategory,
        bool bAllowAbsoluteFallback,
        bool bStealth)
    {
        if (!signature.Name || !signature.Pattern || !detour || !originalOut)
            return false;

        Register(
            signature.Name,
            signature.Pattern,
            signature.Timing,
            signature.bResolveRipRelative,
            signature.RipDisplacementOffset,
            signature.RipInstructionLength);

        auto it = g_Signatures.find(std::string(signature.Name));
        if (it == g_Signatures.end())
            return false;

        auto& entry = it->second;
        if (entry.bHookInstalled)
            return true;

        const uintptr_t targetAddress = Resolve(signature.Name);
        if (!targetAddress)
            return false;

        void* target = reinterpret_cast<void*>(targetAddress);

        if (bStealth)
        {
            // Note: Standard MinHook fallback is now the default for code segments.
            // We use StealthVirtualProtect inside our engine hooks for protection-level adjustments.
        }

        MH_STATUS createStatus = MH_CreateHook(target, detour, reinterpret_cast<LPVOID*>(originalOut));
        if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
        {
            entry.ConsecutiveInstallFailures++;
            const int failureCount = entry.ConsecutiveInstallFailures;

            const bool bShouldTryImmediateFallback =
                bAllowAbsoluteFallback &&
                fallbackLen != 0 &&
                createStatus == MH_ERROR_MEMORY_ALLOC;
            const bool bShouldTryDelayedFallback =
                bAllowAbsoluteFallback &&
                fallbackLen != 0 &&
                failureCount >= 3;

            if (bShouldTryImmediateFallback || bShouldTryDelayedFallback)
            {
                if (TryInstallAbsoluteFallback(
                        entry,
                        signature,
                        target,
                        detour,
                        originalOut,
                        fallbackLen,
                        logCategory,
                        targetAddress,
                        failureCount))
                {
                    return true;
                }
            }

            LOG_WARN(
                logCategory ? logCategory : "Signature",
                "MH_CreateHook failed for '%s': %d (failure %d/3 before detour fallback)",
                signature.Name,
                static_cast<int>(createStatus),
                entry.ConsecutiveInstallFailures);
            return false;
        }

        MH_STATUS enableStatus = MH_EnableHook(target);
        if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
        {
            entry.ConsecutiveInstallFailures++;
            LOG_WARN(
                logCategory ? logCategory : "Signature",
                "MH_EnableHook failed for '%s': %d",
                signature.Name,
                static_cast<int>(enableStatus));
            return false;
        }

        entry.bHookInstalled = true;
        entry.ConsecutiveInstallFailures = 0;
        LOG_DEBUG(
            logCategory ? logCategory : "Signature",
            "SUCCESS: '%s' hook installed at 0x%llX",
            signature.Name,
            static_cast<unsigned long long>(targetAddress));
        return true;
    }
}
