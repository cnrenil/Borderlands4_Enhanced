#pragma once

#include <string>

namespace SignatureRegistry
{
    struct Signature
    {
        const char* Name;
        const char* Pattern;
    };

    void Clear();
    void Register(const char* name, const char* pattern);
    uintptr_t Resolve(const char* name);
    uintptr_t Resolve(const Signature& signature);
}
