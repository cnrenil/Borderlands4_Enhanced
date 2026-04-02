#pragma once
#include <string>
#include <windows.h>

namespace StealthHook
{
    /**
     * Initializes the StealthHooking system (resolves syscall numbers and registers VEH).
     */
    bool Initialize();

    /**
     * Restores all hooks and removes the VEH.
     */
    void Shutdown();

    /**
     * Creates a hook registration for a target function.
     * This allocates a trampoline to allow calling the original function.
     * 
     * @param name Unique name for the hook.
     * @param target Address of the function to hook.
     * @param detour Address of your substitute function.
     * @param original Pointer to receive the address of the trampoline (to call the original code).
     */
    bool CreateHook(const std::string& name, void* target, void* detour, void** original);

    /**
     * Enables a previously created hook.
     * For high-stealth, this uses an INT 3 (0xCC) or ICEBP (0xF1) patch
     * handled by the VMT/VEH infrastructure.
     */
    bool EnableHook(const std::string& name);

    /**
     * Disables a hook by restoring the original first byte.
     */
    bool DisableHook(const std::string& name);

    /**
     * Object-based VMT (Virtual Method Table) hijacking.
     * Highly stealthy as it does not modify common code segments.
     * 
     * @param pInstance Pointer to the object instance.
     * @param index VTable index of the function to swap.
     * @param pDetour Your replacement function.
     * @param ppOriginal Pointer to receive the original virtual function address.
     * @param vtableOffset Offset to the VTable pointer within the instance.
     */
    bool HookVMT(void* pInstance, int index, void* pDetour, void** ppOriginal, int vtableOffset = 0);

    /**
     * Stealthily change memory protection using indirect syscalls.
     */
    bool StealthVirtualProtect(void* address, size_t size, uint32_t newProtect, uint32_t* oldProtect);

    /**
     * Internal utility for stealthily patching a single byte using indirect syscalls.
     */
    bool StealthPatchByte(void* address, uint8_t byte);
}
