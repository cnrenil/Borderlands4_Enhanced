#include "pch.h"
#include "StealthHook.h"
#include <mutex>

extern "C" NTSTATUS StealthNtProtectVirtualMemory(
    uint32_t syscallNum,
    void* syscallInstrAddr,
    HANDLE ProcessHandle,
    PVOID* BaseAddress,
    PSIZE_T RegionSize,
    ULONG NewProtect,
    PULONG OldProtect
);

namespace StealthHook
{
    namespace
    {
        uint32_t g_NtProtectSyscallNum = 0x50; 
        void* g_NtSyscallInstr = nullptr;

        uint32_t ResolveSyscallNumber(const char* funcName)
        {
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            if (!ntdll) return 0;
            uint8_t* pFunc = (uint8_t*)GetProcAddress(ntdll, funcName);
            if (!pFunc) return 0;

            for (uint32_t i = 0; i < 64; ++i)
            {
                if (pFunc[i] == 0xB8)
                {
                    uint32_t syscallNum = *(uint32_t*)(pFunc + i + 1);
                    if (syscallNum < 2048)
                    {
                        for (uint32_t j = 0; j < 128; ++j)
                        {
                            if (pFunc[j] == 0x0F && pFunc[j + 1] == 0x05)
                            {
                                g_NtSyscallInstr = (void*)(pFunc + j);
                                return syscallNum;
                            }
                        }
                    }
                }
            }
            return 0;
        }
    }

    bool Initialize()
    {
        g_NtProtectSyscallNum = ResolveSyscallNumber("NtProtectVirtualMemory");
        if (g_NtProtectSyscallNum == 0) return false;
        
        LOG_INFO("StealthHook", "Initialized with VMT primary and Indirect Syscall support.");
        return true;
    }

    void Shutdown() {}

    bool StealthVirtualProtect(void* address, size_t size, uint32_t newProtect, uint32_t* oldProtect)
    {
        if (!address || size == 0) return false;

        // If Initialize() was skipped or failed, lazily resolve once here so callers
        // don't jump through a null syscall instruction pointer.
        if (!g_NtSyscallInstr || g_NtProtectSyscallNum == 0)
        {
            g_NtProtectSyscallNum = ResolveSyscallNumber("NtProtectVirtualMemory");
            if (!g_NtSyscallInstr || g_NtProtectSyscallNum == 0)
            {
                return false;
            }
        }

        PVOID baseAddr = address;
        SIZE_T regionSize = size;
        ULONG old;

        NTSTATUS status = StealthNtProtectVirtualMemory(
            g_NtProtectSyscallNum,
            g_NtSyscallInstr,
            GetCurrentProcess(),
            &baseAddr,
            &regionSize,
            newProtect,
            &old
        );

        if (oldProtect) *oldProtect = (uint32_t)old;
        return status == 0;
    }

    bool StealthPatchByte(void* address, uint8_t byte)
    {
        uint32_t oldProtect;
        if (!StealthVirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        *(uint8_t*)address = byte;

        return StealthVirtualProtect(address, 1, oldProtect, &oldProtect);
    }

    bool HookVMT(void* pInstance, int index, void* pDetour, void** ppOriginal, int vtableOffset)
    {
        if (!pInstance || index < 0 || !pDetour) return false;

        // Correct for secondary VTables by applying the instance offset
        uintptr_t instanceHead = reinterpret_cast<uintptr_t>(pInstance);
        void*** vtablePtr = reinterpret_cast<void***>(instanceHead + vtableOffset);
        if (!vtablePtr || !*vtablePtr) return false;

        void** originalVtable = *vtablePtr;

        // Allocate a shadow vtable (non-instrumented memory)
        static constexpr int kMaxVTableSize = 512;
        void** shadowVtable = (void**)VirtualAlloc(NULL, kMaxVTableSize * sizeof(void*), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!shadowVtable) return false;

        memcpy(shadowVtable, originalVtable, kMaxVTableSize * sizeof(void*));

        // Swap function
        *ppOriginal = originalVtable[index];
        shadowVtable[index] = pDetour;

        // Make the new vtable executable
        DWORD oldProtect;
        VirtualProtect(shadowVtable, kMaxVTableSize * sizeof(void*), PAGE_EXECUTE_READ, &oldProtect);

        // Stealthily swap the instance pointer using indirect syscall
        uint32_t vptrOldProt;
        if (StealthVirtualProtect(vtablePtr, sizeof(void*), PAGE_READWRITE, &vptrOldProt))
        {
            *vtablePtr = shadowVtable;
            StealthVirtualProtect(vtablePtr, sizeof(void*), vptrOldProt, &vptrOldProt);
            return true;
        }
        
        return false;
    }

    // Stub for removed functionality to avoid linker errors if some files still reference them briefly
    bool CreateHook(const std::string& name, void* target, void* detour, void** original) { return false; }
    bool EnableHook(const std::string& name) { return false; }
    bool DisableHook(const std::string& name) { return false; }
}
