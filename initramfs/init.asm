bits 64

section .text
global _start

_start:
    xor rax, rax
    int 0x3

.loop:
    pause
    jmp .loop
