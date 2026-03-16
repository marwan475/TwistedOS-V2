void _start()
{
    __asm__ __volatile__("mov $2, %%rax\n"
                         "mov $111, %%rdi\n"
                         "mov $222, %%rsi\n"
                         "mov $333, %%rdx\n"
                         "mov $444, %%r10\n"
                         "mov $555, %%r8\n"
                         "mov $456, %%r9\n"
                         "syscall\n"
                         :
                         :
                         : "rax", "rdi", "rsi", "rdx", "r10", "r8", "r9", "rcx", "r11", "memory");

    while (1)
    {
        __asm__ __volatile__("pause");
    }
}
