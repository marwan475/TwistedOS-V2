bits 64

section .text
global _start

_start:
.loop:
    pause
    jmp .loop
