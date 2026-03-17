bits 64
default rel

section .text

global ResourceLayerTaskSwitchKernelAsm
global ResourceLayerTaskSwitchUserAsm

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
%define USER_DPL_MASK    0x3

; /**
;  * Function: ResourceLayerTaskSwitchKernelAsm
;  * Description: Saves current task CPU state when needed and restores next kernel task CPU state.
;  * Parameters:
;  *   rdi (CpuState*) - Current task state storage.
;  *   rsi (const CpuState*) - Next task state to restore.
;  * Returns:
;  *   void - Control transfers to restored RIP via ret.
;  */
ResourceLayerTaskSwitchKernelAsm:
    ; SysV ABI:
    ; rdi = CpuState* old_state
    ; rsi = const CpuState* new_state

    pushfq
    cli

    test byte [rdi + CPUSTATE_CS], USER_DPL_MASK
    jnz .skip_save_old_state_discard_flags

    mov [rdi + CPUSTATE_RAX], rax
    mov [rdi + CPUSTATE_RCX], rcx
    mov [rdi + CPUSTATE_RDX], rdx
    mov [rdi + CPUSTATE_RBX], rbx
    mov [rdi + CPUSTATE_RBP], rbp
    mov [rdi + CPUSTATE_RSI], rsi
    mov [rdi + CPUSTATE_RDI], rdi
    mov [rdi + CPUSTATE_R8],  r8
    mov [rdi + CPUSTATE_R9],  r9
    mov [rdi + CPUSTATE_R10], r10
    mov [rdi + CPUSTATE_R11], r11
    mov [rdi + CPUSTATE_R12], r12
    mov [rdi + CPUSTATE_R13], r13
    mov [rdi + CPUSTATE_R14], r14
    mov [rdi + CPUSTATE_R15], r15

    lea rax, [rel TaskSwitchKernelResume]
    mov [rdi + CPUSTATE_RIP], rax

    pop qword [rdi + CPUSTATE_RFLAGS]

    mov ax, cs
    movzx rax, ax
    mov [rdi + CPUSTATE_CS], rax

    mov ax, ss
    movzx rax, ax
    mov [rdi + CPUSTATE_SS], rax

    mov [rdi + CPUSTATE_RSP], rsp

    jmp .skip_save_old_state

.skip_save_old_state_discard_flags:

    add rsp, 8

.skip_save_old_state:

    mov r11, rsi
    mov rsp, [r11 + CPUSTATE_RSP]

    mov rax, [r11 + CPUSTATE_RAX]
    mov rcx, [r11 + CPUSTATE_RCX]
    mov rdx, [r11 + CPUSTATE_RDX]
    mov rbx, [r11 + CPUSTATE_RBX]
    mov rbp, [r11 + CPUSTATE_RBP]
    mov rsi, [r11 + CPUSTATE_RSI]
    mov rdi, [r11 + CPUSTATE_RDI]
    mov r8,  [r11 + CPUSTATE_R8]
    mov r9,  [r11 + CPUSTATE_R9]
    mov r10, [r11 + CPUSTATE_R10]
    mov r12, [r11 + CPUSTATE_R12]
    mov r13, [r11 + CPUSTATE_R13]
    mov r14, [r11 + CPUSTATE_R14]
    mov r15, [r11 + CPUSTATE_R15]

    push qword [r11 + CPUSTATE_RIP]
    push qword [r11 + CPUSTATE_RFLAGS]
    popfq

    mov r11, [r11 + CPUSTATE_R11]
    ret

; /**
;  * Function: ResourceLayerTaskSwitchUserAsm
;  * Description: Saves current task CPU state when needed, optionally switches page table, restores next user task state, and enters user mode.
;  * Parameters:
;  *   rdi (CpuState*) - Current task state storage.
;  *   rsi (const CpuState*) - Next task state to restore.
;  *   rdx (uint64_t) - Optional CR3 value for next user address space.
;  * Returns:
;  *   void - Control transfers to restored user RIP via iretq.
;  */
ResourceLayerTaskSwitchUserAsm:
    ; SysV ABI:
    ; rdi = CpuState* old_state
    ; rsi = const CpuState* new_state
    ; rdx = uint64_t page_map_l4_table_addr

    pushfq
    cli

    test byte [rdi + CPUSTATE_CS], USER_DPL_MASK
    jnz .skip_save_old_state_discard_flags

    mov [rdi + CPUSTATE_RAX], rax
    mov [rdi + CPUSTATE_RCX], rcx
    mov [rdi + CPUSTATE_RDX], rdx
    mov [rdi + CPUSTATE_RBX], rbx
    mov [rdi + CPUSTATE_RBP], rbp
    mov [rdi + CPUSTATE_RSI], rsi
    mov [rdi + CPUSTATE_RDI], rdi
    mov [rdi + CPUSTATE_R8],  r8
    mov [rdi + CPUSTATE_R9],  r9
    mov [rdi + CPUSTATE_R10], r10
    mov [rdi + CPUSTATE_R11], r11
    mov [rdi + CPUSTATE_R12], r12
    mov [rdi + CPUSTATE_R13], r13
    mov [rdi + CPUSTATE_R14], r14
    mov [rdi + CPUSTATE_R15], r15

    lea rax, [rel TaskSwitchUserResume]
    mov [rdi + CPUSTATE_RIP], rax

    pop qword [rdi + CPUSTATE_RFLAGS]

    mov ax, cs
    movzx rax, ax
    mov [rdi + CPUSTATE_CS], rax

    mov ax, ss
    movzx rax, ax
    mov [rdi + CPUSTATE_SS], rax

    mov [rdi + CPUSTATE_RSP], rsp

    jmp .skip_save_old_state

.skip_save_old_state_discard_flags:

    add rsp, 8

.skip_save_old_state:

    test rdx, rdx
    jz .skip_user_cr3
    mov cr3, rdx

.skip_user_cr3:

    mov r11, rsi
    mov rsp, [r11 + CPUSTATE_RSP]

    mov rax, [r11 + CPUSTATE_RAX]
    mov rcx, [r11 + CPUSTATE_RCX]
    mov rdx, [r11 + CPUSTATE_RDX]
    mov rbx, [r11 + CPUSTATE_RBX]
    mov rbp, [r11 + CPUSTATE_RBP]
    mov rsi, [r11 + CPUSTATE_RSI]
    mov rdi, [r11 + CPUSTATE_RDI]
    mov r8,  [r11 + CPUSTATE_R8]
    mov r9,  [r11 + CPUSTATE_R9]
    mov r10, [r11 + CPUSTATE_R10]
    mov r12, [r11 + CPUSTATE_R12]
    mov r13, [r11 + CPUSTATE_R13]
    mov r14, [r11 + CPUSTATE_R14]
    mov r15, [r11 + CPUSTATE_R15]

    push qword [r11 + CPUSTATE_SS]
    push qword [r11 + CPUSTATE_RSP]
    push qword [r11 + CPUSTATE_RFLAGS]
    push qword [r11 + CPUSTATE_CS]
    push qword [r11 + CPUSTATE_RIP]

    mov r11, [r11 + CPUSTATE_R11]
    iretq

; /**
;  * Function: TaskSwitchKernelResume
;  * Description: Resume label used as saved RIP target when returning to a previously switched-out kernel task.
;  * Parameters:
;  *   None
;  * Returns:
;  *   void - Returns to caller using ret.
;  */
TaskSwitchKernelResume:
    ret

; /**
;  * Function: TaskSwitchUserResume
;  * Description: Resume label used as saved RIP target for user-task context save path.
;  * Parameters:
;  *   None
;  * Returns:
;  *   void - Returns to caller using ret.
;  */
TaskSwitchUserResume:
    ret