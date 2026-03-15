#pragma once
#include <vector>
#include <string>
#include <Windows.h>

namespace Memory
{
    uintptr_t FindPattern(const char* pattern);
    uintptr_t FindPattern(HMODULE hModule, const char* pattern);
    uintptr_t FindPatternGlobal(const char* pattern);
    
    // Scans a specific range
    uintptr_t ScanRange(uintptr_t start, uintptr_t end, const char* pattern);

    // Resolve a near-call (E8 rel32) target from an instruction address.
    uintptr_t ResolveRelativeCallTarget(uintptr_t callInstructionAddress);
}
