; File: syscall.asm
; Author: Marwan Mostafa
; Description: System call entry and return assembly routines.

bits 64
default rel

section .text

global SystemCallEntry
global GetSavedSystemCallFrame
global LoadSavedSystemCallCpuState

extern HandleSystemCallFromEntry
extern GetCurrentProcessCpuStateForSyscallReturn
extern KernelTSS

%define TSS_RSP0_LOWER_OFFSET_BYTES 4
%define USER_CS 0x1B
%define USER_SS 0x23

%define CPUSTATE_RAX     0
%define CPUSTATE_RCX     8
%define CPUSTATE_RDX     16
%define CPUSTATE_RBX     24
%define CPUSTATE_RBP     32
%define CPUSTATE_RSI     40
%define CPUSTATE_RDI     48
%define CPUSTATE_R8      56
%define CPUSTATE_R9      64
%define CPUSTATE_R10     72
%define CPUSTATE_R11     80
%define CPUSTATE_R12     88
%define CPUSTATE_R13     96
%define CPUSTATE_R14     104
%define CPUSTATE_R15     112
%define CPUSTATE_RIP     120
%define CPUSTATE_RFLAGS  128
%define CPUSTATE_RSP     136
%define CPUSTATE_CS      144
%define CPUSTATE_SS      152

; /**
;  * Function: SystemCallEntry
;  * Description: SYSCALL entry trampoline that saves user return context, switches to kernel stack, dispatches syscall handler,
;  *              then returns either to the saved syscall frame or (for successful execve) to the current process state.
;  * Parameters:
;  *   rax, rdi, rsi, rdx, r10, r8, r9 (implicit) - Linux/SysV syscall register arguments.
;  * Returns:
;  *   void - Returns to user space via iretq.
;  */
SystemCallEntry:
    mov [SavedSystemCallUserRSP], rsp
    mov [SavedSystemCallUserRIP], rcx
    mov [SavedSystemCallUserRFLAGS], r11
    mov [SavedSystemCallUserRAX], rax
    mov [SavedSystemCallUserRDX], rdx
    mov [SavedSystemCallUserRBX], rbx
    mov [SavedSystemCallUserRBP], rbp
    mov [SavedSystemCallUserRSI], rsi
    mov [SavedSystemCallUserRDI], rdi
    mov [SavedSystemCallUserR8],  r8
    mov [SavedSystemCallUserR9],  r9
    mov [SavedSystemCallUserR10], r10
    mov [SavedSystemCallUserR12], r12
    mov [SavedSystemCallUserR13], r13
    mov [SavedSystemCallUserR14], r14
    mov [SavedSystemCallUserR15], r15

    mov rsp, qword [KernelTSS + TSS_RSP0_LOWER_OFFSET_BYTES]

    mov rcx, r10
    push rax
    call HandleSystemCallFromEntry

    cmp qword [rsp], 59
    jne .return_to_saved_syscall_frame

    test rax, rax
    jne .return_to_saved_syscall_frame

    call GetCurrentProcessCpuStateForSyscallReturn
    test rax, rax
    jz .return_to_saved_syscall_frame

    mov r11, rax

    push qword [r11 + 152]
    push qword [r11 + 136]
    push qword [r11 + 128]
    push qword [r11 + 144]
    push qword [r11 + 120]
    iretq

.return_to_saved_syscall_frame:
    add rsp, 8

    mov rcx, [SavedSystemCallUserRIP]
    mov r11, [SavedSystemCallUserRFLAGS]
    mov r10, [SavedSystemCallUserRSP]

    push USER_SS
    push r10
    push r11
    push USER_CS
    push rcx
    iretq

GetSavedSystemCallFrame:
    test rdi, rdi
    jz .skip_rip
    mov rax, [SavedSystemCallUserRIP]
    mov [rdi], rax

.skip_rip:
    test rsi, rsi
    jz .skip_rsp
    mov rax, [SavedSystemCallUserRSP]
    mov [rsi], rax

.skip_rsp:
    test rdx, rdx
    jz .done_get
    mov rax, [SavedSystemCallUserRFLAGS]
    mov [rdx], rax

.done_get:
    ret

LoadSavedSystemCallCpuState:
    test rdi, rdi
    jz .done_load

    mov rax, [SavedSystemCallUserRAX]
    mov [rdi + CPUSTATE_RAX], rax

    xor rax, rax
    mov [rdi + CPUSTATE_RCX], rax

    mov rax, [SavedSystemCallUserRDX]
    mov [rdi + CPUSTATE_RDX], rax

    mov rax, [SavedSystemCallUserRBX]
    mov [rdi + CPUSTATE_RBX], rax

    mov rax, [SavedSystemCallUserRBP]
    mov [rdi + CPUSTATE_RBP], rax

    mov rax, [SavedSystemCallUserRSI]
    mov [rdi + CPUSTATE_RSI], rax

    mov rax, [SavedSystemCallUserRDI]
    mov [rdi + CPUSTATE_RDI], rax

    mov rax, [SavedSystemCallUserR8]
    mov [rdi + CPUSTATE_R8], rax

    mov rax, [SavedSystemCallUserR9]
    mov [rdi + CPUSTATE_R9], rax

    mov rax, [SavedSystemCallUserR10]
    mov [rdi + CPUSTATE_R10], rax

    xor rax, rax
    mov [rdi + CPUSTATE_R11], rax

    mov rax, [SavedSystemCallUserR12]
    mov [rdi + CPUSTATE_R12], rax

    mov rax, [SavedSystemCallUserR13]
    mov [rdi + CPUSTATE_R13], rax

    mov rax, [SavedSystemCallUserR14]
    mov [rdi + CPUSTATE_R14], rax

    mov rax, [SavedSystemCallUserR15]
    mov [rdi + CPUSTATE_R15], rax

    mov rax, [SavedSystemCallUserRIP]
    mov [rdi + CPUSTATE_RIP], rax

    mov rax, [SavedSystemCallUserRFLAGS]
    mov [rdi + CPUSTATE_RFLAGS], rax

    mov rax, [SavedSystemCallUserRSP]
    mov [rdi + CPUSTATE_RSP], rax

    mov rax, USER_CS
    mov [rdi + CPUSTATE_CS], rax

    mov rax, USER_SS
    mov [rdi + CPUSTATE_SS], rax

.done_load:
    ret

section .bss
align 8
SavedSystemCallUserRSP: resq 1
SavedSystemCallUserRIP: resq 1
SavedSystemCallUserRFLAGS: resq 1
SavedSystemCallUserRAX: resq 1
SavedSystemCallUserRDX: resq 1
SavedSystemCallUserRBX: resq 1
SavedSystemCallUserRBP: resq 1
SavedSystemCallUserRSI: resq 1
SavedSystemCallUserRDI: resq 1
SavedSystemCallUserR8:  resq 1
SavedSystemCallUserR9:  resq 1
SavedSystemCallUserR10: resq 1
SavedSystemCallUserR12: resq 1
SavedSystemCallUserR13: resq 1
SavedSystemCallUserR14: resq 1
SavedSystemCallUserR15: resq 1
