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

    // Patch instance vtable slot to detour and optionally capture original.
    bool PatchVTableSlot(void* instance, size_t slotIndex, void* detour, void** originalOut);

    // Install a simple absolute jump detour and build a trampoline with stolen bytes.
    // stolenLen must cover whole instructions and be at least 12 bytes on x64.
    bool HookFunctionAbsolute(void* target, void* detour, void** originalOut, size_t stolenLen);

    // Calculate a safe stolen-byte length that covers whole instructions and is at least minLen.
    // Returns 0 on decode failure or if maxLen is exceeded.
    size_t CalculateSafeHookLength(void* target, size_t minLen, size_t maxLen = 64);

    // Dump vtable slots around centerIndex for diagnostics.
    void DumpVTableWindow(
        void** vtable,
        size_t centerIndex,
        size_t radius,
        const char* tag,
        const char* logTag = "MemoryDbg");
}
