void _start()
{
    __asm__ __volatile__("mov $1, %%rax\n"
                         "mov $11, %%rdi\n"
                         "mov $22, %%rsi\n"
                         "mov $33, %%rdx\n"
                         "mov $44, %%r10\n"
                         "mov $55, %%r8\n"
                         "mov $66, %%r9\n"
                         "syscall\n"
                         :
                         :
                         : "rax", "rdi", "rsi", "rdx", "r10", "r8", "r9", "rcx", "r11", "memory");

    while (1)
    {
        __asm__ __volatile__("pause");
    }
}
