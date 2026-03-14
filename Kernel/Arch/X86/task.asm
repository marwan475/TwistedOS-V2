bits 64
default rel

section .text

global ResourceLayerTaskSwitchAsm

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

ResourceLayerTaskSwitchAsm:
    ; SysV ABI:
    ; rdi = CpuState* old_state
    ; rsi = void** old_stack
    ; rdx = const CpuState* new_state
    ; rcx = void* new_stack

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

    lea rax, [rel .task_switch_resume]
    mov [rdi + CPUSTATE_RIP], rax

    pushfq
    pop qword [rdi + CPUSTATE_RFLAGS]

    mov [rsi], rsp

    mov r11, rdx
    mov rsp, rcx

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

.task_switch_resume:
    ret