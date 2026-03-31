#include "pch.h"
// Thanks to https://github.com/bl-sdk/unrealsdk for the anti-debug approach reference.

namespace AntiDebug
{
    namespace
    {
        constexpr size_t kAssumedMinPageSize = 0x1000;
        constexpr size_t kSymbioteHookLen = 16;

        bool PatchByte(void* address, uint8_t value)
        {
            if (!address)
                return false;

            DWORD oldProtect = 0;
            if (!VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
            {
                LOG_WARN("AntiDebug", "VirtualProtect failed at 0x%llX", reinterpret_cast<uintptr_t>(address));
                return false;
            }

            *reinterpret_cast<uint8_t*>(address) = value;

            DWORD restoredProtect = 0;
            VirtualProtect(address, 1, oldProtect, &restoredProtect);
            FlushInstructionCache(GetCurrentProcess(), address, 1);
            return true;
        }

        void RemoveNoAccessPages(uintptr_t base, size_t size)
        {
            for (uintptr_t ptr = base; ptr < (base + size);)
            {
                MEMORY_BASIC_INFORMATION mem{};
                if (VirtualQuery(reinterpret_cast<void*>(ptr), &mem, sizeof(mem)) == 0)
                {
                    LOG_WARN("AntiDebug", "Failed to query memory at 0x%llX", ptr);
                    ptr += kAssumedMinPageSize;
                    continue;
                }

                ptr = reinterpret_cast<uintptr_t>(mem.BaseAddress) + mem.RegionSize;

                if (mem.State != MEM_COMMIT || mem.Protect != PAGE_NOACCESS)
                    continue;

                LOG_INFO("AntiDebug", "Unlocking PAGE_NOACCESS at 0x%llX", reinterpret_cast<uintptr_t>(mem.BaseAddress));

                DWORD oldProtect = 0;
                if (VirtualProtect(mem.BaseAddress, mem.RegionSize, PAGE_READONLY, &oldProtect) == 0)
                {
                    LOG_ERROR(
                        "AntiDebug",
                        "Failed to unlock page at 0x%llX: %lu",
                        reinterpret_cast<uintptr_t>(mem.BaseAddress),
                        GetLastError());
                }
            }
        }

        bool GetExeRange(uintptr_t& outBase, size_t& outSize)
        {
            HMODULE module = GetModuleHandle(nullptr);
            if (!module)
                return false;

            auto* dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
            auto* ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(
                reinterpret_cast<uintptr_t>(module) + dosHeader->e_lfanew);

            outBase = reinterpret_cast<uintptr_t>(module);
            outSize = ntHeaders->OptionalHeader.SizeOfImage;
            return true;
        }

        void RestoreAntiDebugFunctions()
        {
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            if (!ntdll)
            {
                LOG_WARN("AntiDebug", "Couldn't find ntdll.dll, couldn't restore anti-debug");
                return;
            }

            auto* dbgBreakPoint = reinterpret_cast<uint8_t*>(GetProcAddress(ntdll, "DbgBreakPoint"));
            if (dbgBreakPoint != nullptr)
            {
                PatchByte(dbgBreakPoint, 0xCC);
            }

            auto* dbgUiRemoteBreakin = reinterpret_cast<uint8_t*>(GetProcAddress(ntdll, "DbgUiRemoteBreakin"));
            if (dbgUiRemoteBreakin != nullptr)
            {
                PatchByte(dbgUiRemoteBreakin, 0x48);
            }
        }

        [[noreturn]] void __stdcall SymbioteHook()
        {
            RestoreAntiDebugFunctions();
            LOG_INFO("AntiDebug", "Killing symbiote thread");
            TerminateThread(GetCurrentThread(), 0);
            for (;;)
            {
            }
        }

        void InternalBypass()
        {
            uintptr_t exeBase = 0;
            size_t exeSize = 0;
            if (GetExeRange(exeBase, exeSize))
            {
                // RemoveNoAccessPages(exeBase, exeSize);
            }

            const bool bDebugEnabled = ConfigManager::Exists("Misc.Debug") && ConfigManager::B("Misc.Debug");
            if (!bDebugEnabled)
            {
                return;
            }

            RestoreAntiDebugFunctions();

            static constexpr SignatureRegistry::Signature kSymbioteEntryPattern{
                "AntiDebug.SymbioteEntryCall",
                "E8 ? ? ? ? 55 48 8B EC 48 83 E4 F0",
                SignatureRegistry::HookTiming::Immediate
            };

            uintptr_t callAddress = SignatureRegistry::Resolve(kSymbioteEntryPattern);
            if (!callAddress)
            {
                callAddress = Memory::FindPattern(kSymbioteEntryPattern.Pattern);
            }

            if (!callAddress)
            {
                LOG_WARN("AntiDebug", "Failed to find symbiote entry call pattern");
                return;
            }

            const uintptr_t symbioteEntryPoint = Memory::ResolveRelativeCallTarget(callAddress);
            if (!symbioteEntryPoint)
            {
                LOG_WARN("AntiDebug", "Failed to resolve symbiote entry point from 0x%llX", callAddress);
                return;
            }

            const size_t safeHookLen = Memory::CalculateSafeHookLength(
                reinterpret_cast<void*>(symbioteEntryPoint),
                kSymbioteHookLen);
            if (safeHookLen == 0)
            {
                LOG_WARN("AntiDebug", "Failed to compute safe hook length for symbiote entry point at 0x%llX", symbioteEntryPoint);
                return;
            }

            void* originalSymbiote = nullptr;
            if (!Memory::HookFunctionAbsolute(
                    reinterpret_cast<void*>(symbioteEntryPoint),
                    reinterpret_cast<void*>(&SymbioteHook),
                    &originalSymbiote,
                    safeHookLen))
            {
                LOG_ERROR("AntiDebug", "Failed to hook symbiote entry point at 0x%llX", symbioteEntryPoint);
                return;
            }

            RestoreAntiDebugFunctions();
            LOG_INFO("AntiDebug", "Hooked symbiote entry point at 0x%llX", symbioteEntryPoint);

            if (ConfigManager::Exists("Misc.Debug") &&
                ConfigManager::B("Misc.Debug") &&
                IsDebuggerPresent() == 0)
            {
                LOG_WARN("AntiDebug", "Debugger not attached yet; anti-debug bypass remains armed");
            }
        }
    }

    void Bypass()
    {
        __try
        {
            InternalBypass();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Logger::RawLog(Logger::Level::Error, "AntiDebug", "Critical error during anti-debug bypass");
        }
    }
}
