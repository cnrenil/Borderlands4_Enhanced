; StealthSyscall.asm
; Assembly implementation of NtProtectVirtualMemory syscall.

.code

; extern "C" NTSTATUS StealthNtProtectVirtualMemory(
;     uint32_t syscallNum,         ; rcx
;     void* syscallInstrAddr,      ; rdx (Address of 'syscall; ret' in ntdll)
;     HANDLE ProcessHandle,        ; r8
;     PVOID* BaseAddress,          ; r9
;     PSIZE_T RegionSize,          ; [rsp+40]
;     ULONG NewProtect,            ; [rsp+48]
;     PULONG OldProtect            ; [rsp+56] (Shadow for r9 is [rsp+32], so 7th arg is at 48+8=56)
; );

StealthNtProtectVirtualMemory PROC
    ; Save r10 as it's required for syscalls
    mov     r10, r8                 ; ProcessHandle -> r10
    mov     rax, rdx                ; syscallInstrAddr -> rax
    mov     r11, rcx                ; syscallNum -> r11
    
    ; Adjust parameters for the actual syscall:
    ; NtProtectVirtualMemory(r10: ProcessHandle, rdx: BaseAddress, r8: RegionSize, r9: NewProtect, [stack]: OldProtect)
    
    mov     rdx, r9                 ; BaseAddress (r9) -> rdx
    mov     r8, [rsp+40]            ; RegionSize ([rsp+40]) -> r8
    mov     r9d, dword ptr [rsp+48] ; NewProtect ([rsp+48]) -> r9d (32-bit)
    
    ; Load OldProtect (7th arg at [rsp+56]) and place it at [rsp+40]
    ; for the syscall's 5th parameter.
    mov     rcx, [rsp+56]           ; OldProtect (PULONG) -> rcx
    mov     [rsp+40], rcx           ; place at [rsp+40]

    mov     eax, r11d               ; syscallNum (r11d) -> eax
    jmp     rax                     ; Jump to 'syscall; ret' in ntdll
    ret
StealthNtProtectVirtualMemory ENDP

END
