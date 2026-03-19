; File: syscall.asm
; Author: Marwan Mostafa
; Description: System call entry and return assembly routines.

bits 64
default rel

section .text

global SystemCallEntry
global SavedSystemCallUserRSP
global SavedSystemCallUserRIP
global SavedSystemCallUserRFLAGS
global SavedSystemCallUserRAX
global SavedSystemCallUserRDX
global SavedSystemCallUserRBX
global SavedSystemCallUserRBP
global SavedSystemCallUserRSI
global SavedSystemCallUserRDI
global SavedSystemCallUserR8
global SavedSystemCallUserR9
global SavedSystemCallUserR10
global SavedSystemCallUserR12
global SavedSystemCallUserR13
global SavedSystemCallUserR14
global SavedSystemCallUserR15

extern HandleSystemCallFromEntry
extern GetCurrentProcessCpuStateForSyscallReturn
extern PersistCurrentSavedSystemCallFrame
extern RestoreCurrentSavedSystemCallFrame
extern CompleteCurrentSystemCallReturn
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

    call PersistCurrentSavedSystemCallFrame

    mov rdi, [SavedSystemCallUserRDI]
    mov rsi, [SavedSystemCallUserRSI]
    mov rdx, [SavedSystemCallUserRDX]
    mov rcx, [SavedSystemCallUserR10]
    mov r8,  [SavedSystemCallUserR8]
    mov r9,  [SavedSystemCallUserR9]
    mov rax, [SavedSystemCallUserRAX]

    push rax
    call HandleSystemCallFromEntry

    cmp qword [rsp], 59
    jne .return_to_saved_syscall_frame

    test rax, rax
    jne .return_to_saved_syscall_frame

    call CompleteCurrentSystemCallReturn

    call GetCurrentProcessCpuStateForSyscallReturn
    test rax, rax
    jz .return_to_saved_syscall_frame

    mov r11, rax
    mov rcx, r11

    mov rdx, [rcx + CPUSTATE_RDX]
    mov rbx, [rcx + CPUSTATE_RBX]
    mov rbp, [rcx + CPUSTATE_RBP]
    mov rsi, [rcx + CPUSTATE_RSI]
    mov rdi, [rcx + CPUSTATE_RDI]
    mov r8,  [rcx + CPUSTATE_R8]
    mov r9,  [rcx + CPUSTATE_R9]
    mov r10, [rcx + CPUSTATE_R10]
    mov r12, [rcx + CPUSTATE_R12]
    mov r13, [rcx + CPUSTATE_R13]
    mov r14, [rcx + CPUSTATE_R14]
    mov r15, [rcx + CPUSTATE_R15]
    mov rax, [rcx + CPUSTATE_RAX]
    mov r11, [rcx + CPUSTATE_R11]

    push qword [rcx + CPUSTATE_SS]
    push qword [rcx + CPUSTATE_RSP]
    push qword [rcx + CPUSTATE_RFLAGS]
    push qword [rcx + CPUSTATE_CS]
    push qword [rcx + CPUSTATE_RIP]
    iretq

.return_to_saved_syscall_frame:
    add rsp, 8

    push rax
    call RestoreCurrentSavedSystemCallFrame
    call CompleteCurrentSystemCallReturn
    pop rax

    mov rdx, [SavedSystemCallUserRDX]
    mov rbx, [SavedSystemCallUserRBX]
    mov rbp, [SavedSystemCallUserRBP]
    mov rsi, [SavedSystemCallUserRSI]
    mov rdi, [SavedSystemCallUserRDI]
    mov r8,  [SavedSystemCallUserR8]
    mov r9,  [SavedSystemCallUserR9]
    mov r10, [SavedSystemCallUserR10]
    mov r12, [SavedSystemCallUserR12]
    mov r13, [SavedSystemCallUserR13]
    mov r14, [SavedSystemCallUserR14]
    mov r15, [SavedSystemCallUserR15]

    push USER_SS
    push qword [SavedSystemCallUserRSP]
    push qword [SavedSystemCallUserRFLAGS]
    push USER_CS
    push qword [SavedSystemCallUserRIP]
    iretq

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
