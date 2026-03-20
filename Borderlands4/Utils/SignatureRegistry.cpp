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
            bool bResolveFailed = false;
            bool bHookInstalled = false;
            ULONGLONG LastAttemptMs = 0;
            int ConsecutiveInstallFailures = 0;
        };

        std::unordered_map<std::string, SignatureEntry> g_Signatures;

        uintptr_t g_LastInGameWorld = 0;
        ULONGLONG g_InGameReadySinceMs = 0;
        // Delay signature-backed gameplay hooks a little longer after map entry.
        constexpr ULONGLONG kInGameReadyWarmupMs = 10000;
        constexpr ULONGLONG kInGameRetryIntervalMs = 5000;

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

            if (!Utils::bIsInGame ||
                Utils::bIsLoading ||
                !GVars.World ||
                !GVars.Character ||
                !GVars.PlayerController ||
                !GVars.PlayerController->PlayerCameraManager)
            {
                g_LastInGameWorld = 0;
                g_InGameReadySinceMs = 0;
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

            return (nowMs - g_InGameReadySinceMs) >= kInGameReadyWarmupMs;
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

    void Register(const char* name, const char* pattern, HookTiming timing)
    {
        if (!name || !pattern || name[0] == '\0' || pattern[0] == '\0')
            return;

        auto& entry = g_Signatures[std::string(name)];
        const bool bPatternChanged = entry.Pattern != pattern;
        const bool bTimingChanged = entry.Timing != timing;
        entry.Pattern = pattern;
        entry.Timing = timing;
        if (bPatternChanged)
        {
            entry.CachedAddress = 0;
            entry.bResolveFailed = false;
            entry.bHookInstalled = false;
            entry.ConsecutiveInstallFailures = 0;
        }
        if (bPatternChanged || bTimingChanged)
        {
            entry.LastAttemptMs = 0;
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
        if (!ShouldAttempt(name, entry.Timing))
            return 0;

        if (entry.bResolveFailed)
            return 0;

        if (entry.CachedAddress)
            return entry.CachedAddress;

        const uintptr_t address = Memory::FindPattern(entry.Pattern.c_str());
        if (!address)
        {
            entry.bResolveFailed = true;
            LOG_WARN("Signature", "Pattern not found for '%s'.", name);
            return 0;
        }

        entry.CachedAddress = address;
        LOG_DEBUG("Signature", "Resolved '%s' at 0x%llX", name, static_cast<unsigned long long>(address));
        return entry.CachedAddress;
    }

    uintptr_t Resolve(const Signature& signature)
    {
        Register(signature.Name, signature.Pattern, signature.Timing);
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

    bool EnsureHook(
        const Signature& signature,
        void* detour,
        void** originalOut,
        size_t fallbackLen,
        const char* logCategory,
        bool bAllowAbsoluteFallback)
    {
        if (!signature.Name || !signature.Pattern || !detour || !originalOut)
            return false;

        Register(signature.Name, signature.Pattern, signature.Timing);

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
