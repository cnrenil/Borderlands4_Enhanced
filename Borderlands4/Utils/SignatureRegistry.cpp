#include "pch.h"

#include <unordered_map>

namespace SignatureRegistry
{
    namespace
    {
        struct SignatureEntry
        {
            std::string Pattern;
            uintptr_t CachedAddress = 0;
            bool bResolveFailed = false;
        };

        std::unordered_map<std::string, SignatureEntry> g_Signatures;
    }

    void Clear()
    {
        g_Signatures.clear();
    }

    void Register(const char* name, const char* pattern)
    {
        if (!name || !pattern || name[0] == '\0' || pattern[0] == '\0')
            return;

        auto& entry = g_Signatures[std::string(name)];
        entry.Pattern = pattern;
        entry.CachedAddress = 0;
        entry.bResolveFailed = false;
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
        Register(signature.Name, signature.Pattern);
        return Resolve(signature.Name);
    }
}
