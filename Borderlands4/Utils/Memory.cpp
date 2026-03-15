#include "pch.h"
#include "Memory.h"
#include <Psapi.h>
#include <cstring>

namespace Memory
{
    uintptr_t FindPattern(const char* pattern)
    {
        return FindPattern(GetModuleHandle(NULL), pattern);
    }

    uintptr_t FindPattern(HMODULE hModule, const char* pattern)
    {
        MODULEINFO modInfo = { 0 };
        if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(MODULEINFO)))
            return 0;

        uintptr_t start = (uintptr_t)modInfo.lpBaseOfDll;
        uintptr_t end = start + modInfo.SizeOfImage;

        LOG_DEBUG("Memory", "Scanning module base: 0x%llX, Size: 0x%X (Range: 0x%llX - 0x%llX)", 
            start, modInfo.SizeOfImage, start, end);

        return ScanRange(start, end, pattern);
    }

    uintptr_t FindPatternGlobal(const char* pattern)
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);

        uintptr_t start = (uintptr_t)si.lpMinimumApplicationAddress;
        uintptr_t end = (uintptr_t)si.lpMaximumApplicationAddress;

        // Note: For x64, we might want to cap the end address to avoid scanning non-canonical or kernel space
        // though lpMaximumApplicationAddress usually handles this.
        return ScanRange(start, end, pattern);
    }

    uintptr_t ScanRange(uintptr_t start, uintptr_t end, const char* pattern)
    {
        auto parsePattern = [](const char* pattern) {
            std::vector<int> bytes;
            char* start_ptr = const_cast<char*>(pattern);
            char* end_ptr = start_ptr + strlen(pattern);

            for (char* current = start_ptr; current < end_ptr; ++current) {
                if (*current == ' ') continue;
                if (*current == '?') {
                    if (current + 1 < end_ptr && *(current + 1) == '?') ++current;
                    bytes.push_back(-1);
                }
                else {
                    bytes.push_back((int)strtoul(current, &current, 16));
                    --current; 
                }
            }
            return bytes;
        };

        std::vector<int> patternBytes = parsePattern(pattern);
        size_t patternSize = patternBytes.size();
        int* patternData = patternBytes.data();

        uintptr_t current_chunk = start;
        while (current_chunk < end - patternSize) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery((LPCVOID)current_chunk, &mbi, sizeof(mbi))) 
                break;

            uintptr_t chunk_end = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
            if (chunk_end > end) chunk_end = end;

            // PERFORMANCE OPTIMIZATION: 
            // Only scan memory that is committed and HAS EXECUTE permissions.
            // Symbiote threads and stealth logic MUST reside in executable memory.
            // This skips 99% of useless data segments (stacks, heaps, etc.)
            bool is_executable = (mbi.State == MEM_COMMIT) &&
                                 (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
                                 !(mbi.Protect & PAGE_GUARD);

            if (is_executable) {
                for (uintptr_t i = current_chunk; i < chunk_end - patternSize; ++i) {
                    bool found = true;
                    for (size_t j = 0; j < patternSize; ++j) {
                        if (patternData[j] != -1 && *(uint8_t*)(i + j) != patternData[j]) {
                            found = false;
                            break;
                        }
                    }
                    if (found) return i;
                }
            }
            
            current_chunk = chunk_end;
        }

        return 0;
    }

    uintptr_t ResolveRelativeCallTarget(uintptr_t callInstructionAddress)
    {
        if (!callInstructionAddress) return 0;

        __try
        {
            const uint8_t opcode = *reinterpret_cast<uint8_t*>(callInstructionAddress);
            if (opcode != 0xE8) return 0; // near call rel32

            const int32_t rel = *reinterpret_cast<int32_t*>(callInstructionAddress + 1);
            return callInstructionAddress + 5 + static_cast<intptr_t>(rel);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool PatchVTableSlot(void* instance, size_t slotIndex, void* detour, void** originalOut)
    {
        if (!instance || !detour || !originalOut) return false;

        void*** asVt = reinterpret_cast<void***>(instance);
        if (!asVt || !*asVt) return false;

        void** vtable = *asVt;
        void** slot = &vtable[slotIndex];

        DWORD oldProtect = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            return false;
        }

        if (!*originalOut)
        {
            *originalOut = *slot;
        }
        *slot = detour;

        DWORD tmp = 0;
        VirtualProtect(slot, sizeof(void*), oldProtect, &tmp);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
        return true;
    }

    bool HookFunctionAbsolute(void* target, void* detour, void** originalOut, size_t stolenLen)
    {
        if (!target || !detour || !originalOut || stolenLen < 12)
        {
            return false;
        }

        const size_t kJumpSize = 12; // mov rax, imm64; jmp rax
        const size_t trampolineSize = stolenLen + kJumpSize;
        uint8_t* trampoline = reinterpret_cast<uint8_t*>(
            VirtualAlloc(nullptr, trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!trampoline)
        {
            return false;
        }

        uint8_t* targetBytes = reinterpret_cast<uint8_t*>(target);
        std::memcpy(trampoline, targetBytes, stolenLen);

        uint8_t* trampolineJump = trampoline + stolenLen;
        trampolineJump[0] = 0x48;
        trampolineJump[1] = 0xB8;
        *reinterpret_cast<uint64_t*>(trampolineJump + 2) = reinterpret_cast<uint64_t>(targetBytes + stolenLen);
        trampolineJump[10] = 0xFF;
        trampolineJump[11] = 0xE0;

        DWORD oldProtect = 0;
        if (!VirtualProtect(targetBytes, stolenLen, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }

        targetBytes[0] = 0x48;
        targetBytes[1] = 0xB8;
        *reinterpret_cast<uint64_t*>(targetBytes + 2) = reinterpret_cast<uint64_t>(detour);
        targetBytes[10] = 0xFF;
        targetBytes[11] = 0xE0;
        for (size_t i = kJumpSize; i < stolenLen; ++i)
        {
            targetBytes[i] = 0x90;
        }

        DWORD tmp = 0;
        VirtualProtect(targetBytes, stolenLen, oldProtect, &tmp);
        FlushInstructionCache(GetCurrentProcess(), targetBytes, stolenLen);
        FlushInstructionCache(GetCurrentProcess(), trampoline, trampolineSize);

        if (!*originalOut)
        {
            *originalOut = trampoline;
        }
        return true;
    }

    void DumpVTableWindow(void** vtable, size_t centerIndex, size_t radius, const char* tag, const char* logTag)
    {
        if (!vtable) return;

        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(vtable, &mbi, sizeof(mbi)) == 0 ||
            mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
        {
            Logger::Log(
                Logger::Level::Debug,
                logTag ? logTag : "MemoryDbg",
                "%s vtable unreadable: vtable=%p",
                tag ? tag : "VTable",
                vtable);
            return;
        }

        const size_t begin = (centerIndex > radius) ? (centerIndex - radius) : 0;
        const size_t end = centerIndex + radius;
        for (size_t i = begin; i <= end; ++i)
        {
            Logger::Log(
                Logger::Level::Debug,
                logTag ? logTag : "MemoryDbg",
                "%s vtable[0x%zX] = %p",
                tag ? tag : "VTable",
                i * sizeof(void*),
                vtable[i]);
        }
    }
}
