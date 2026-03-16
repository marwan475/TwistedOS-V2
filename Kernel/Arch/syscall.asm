bits 64
default rel

section .text

global SystemCallEntry

extern HandleSystemCallFromEntry
extern KernelTSS

%define TSS_RSP0_LOWER_OFFSET_BYTES 4
%define USER_CS 0x1B
%define USER_SS 0x23

SystemCallEntry:
    mov [SavedSystemCallUserRSP], rsp
    mov [SavedSystemCallUserRIP], rcx
    mov [SavedSystemCallUserRFLAGS], r11

    mov rsp, qword [KernelTSS + TSS_RSP0_LOWER_OFFSET_BYTES]

    mov rcx, r10
    push rax
    call HandleSystemCallFromEntry
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

section .bss
align 8
SavedSystemCallUserRSP: resq 1
SavedSystemCallUserRIP: resq 1
SavedSystemCallUserRFLAGS: resq 1
