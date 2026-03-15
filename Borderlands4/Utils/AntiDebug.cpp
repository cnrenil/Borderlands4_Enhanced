#include "pch.h"

typedef NTSTATUS(NTAPI* pNtQueryInformationThread)(
    HANDLE          ThreadHandle,
    THREADINFOCLASS ThreadInformationClass,
    PVOID           ThreadInformation,
    ULONG           ThreadInformationLength,
    PULONG          ReturnLength
);

#define ThreadQuerySetWin32StartAddress (THREADINFOCLASS)9

namespace AntiDebug
{
    static void InternalBypass()
    {
        LOG_INFO("AntiDebug", "Starting advanced safe search for Symbiote thread...");

        const char* patterns[] = {
            "55 48 8B EC 48 83 E4 F0 48 81 EC 10 02 00 00 0F AE 04 24 48 81 EC 30 02 00 00",
            "0F AE 04 24 48 81 EC ? ? 00 00 50 51 52 53 55 56 57 B9 F4 01 00 00",
            "55 48 8B EC 48 83 E4 F0 48 81 EC 10 02 00 00"
        };

        std::vector<uintptr_t> found_addrs;
        for (const char* p : patterns) {
            uintptr_t addr = Memory::FindPattern(p);
            
            if (!addr) {
                LOG_DEBUG("AntiDebug", "Pattern not in main module, trying Global scan...");
                addr = Memory::FindPatternGlobal(p);
            }

            if (addr) {
                found_addrs.push_back(addr);
                LOG_INFO("AntiDebug", "Symbiote logic found at 0x%llX. Patching to 'ret'...", addr);

                unsigned char ret_opcode = 0xC3; 
                DWORD old_protect;
                if (VirtualProtect((LPVOID)addr, 1, PAGE_EXECUTE_READWRITE, &old_protect)) {
                    *(unsigned char*)addr = ret_opcode;
                    VirtualProtect((LPVOID)addr, 1, old_protect, &old_protect);
                    LOG_INFO("AntiDebug", "Successfully logic-patched address 0x%llX", addr);
                } else {
                    LOG_ERROR("AntiDebug", "Failed to VirtualProtect for patching at 0x%llX", addr);
                }
            }
        }

        if (found_addrs.empty()) {
            LOG_WARN("AntiDebug", "No Symbiote patterns found in memory. (Maybe hasn't decrypted yet?)");
            return;
        }

        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return;

        THREADENTRY32 te;
        te.dwSize = sizeof(THREADENTRY32);

        HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
        if (!hNtdll) {
            CloseHandle(hSnapshot);
            return;
        }

        auto NtQueryInformationThread = (pNtQueryInformationThread)GetProcAddress(hNtdll, "NtQueryInformationThread");
        if (!NtQueryInformationThread) {
            CloseHandle(hSnapshot);
            return;
        }

        DWORD PID = GetCurrentProcessId();
        int killedCount = 0;

        if (Thread32First(hSnapshot, &te)) {
            do {
                if (te.th32OwnerProcessID == PID && te.th32ThreadID != GetCurrentThreadId()) {
                    HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_TERMINATE, FALSE, te.th32ThreadID);
                    if (hThread) {
                        uintptr_t startAddr = 0;
                        NTSTATUS status = NtQueryInformationThread(hThread, ThreadQuerySetWin32StartAddress, &startAddr, sizeof(startAddr), NULL);

                        if (status == 0) {
                            for (uintptr_t patternAddr : found_addrs) {
                                if (startAddr >= (patternAddr - 0x40) && startAddr <= (patternAddr + 0x40)) {
                                    LOG_INFO("AntiDebug", "Active Symbiote thread detected! ID: %d, Start: 0x%llX. Terminating...", 
                                        te.th32ThreadID, startAddr);
                                    
                                    if (TerminateThread(hThread, 0)) {
                                        killedCount++;
                                    } else {
                                        LOG_WARN("AntiDebug", "Could not kill thread %d (Access Denied?), but logic at 0x%llX was patched.", 
                                            te.th32ThreadID, patternAddr);
                                    }
                                    break; 
                                }
                            }
                        }
                        CloseHandle(hThread);
                    }
                }
            } while (Thread32Next(hSnapshot, &te));
        }

        CloseHandle(hSnapshot);
        LOG_INFO("AntiDebug", "Bypass finished. Killed %d Symbiote threads.", killedCount);
    }

    void Bypass()
    {
        __try 
        {
            InternalBypass();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) 
        {
            Logger::RawLog(Logger::Level::Error, "AntiDebug", "Critical error during Anti-Debug bypass! (SEH Caught)");
        }
    }
}
