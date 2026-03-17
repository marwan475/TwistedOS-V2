typedef unsigned long u64;

static inline long syscall6(u64 number, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6)
{
    register u64 r10 __asm__("r10") = arg4;
    register u64 r8 __asm__("r8") = arg5;
    register u64 r9 __asm__("r9") = arg6;
    long result;

    __asm__ __volatile__("syscall"
                         : "=a"(result)
                         : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");

    return result;
}

static long test_syscall_alpha(void)
{
    return syscall6(2, 111, 222, 333, 444, 555, 456);
}

static long test_syscall_beta(void)
{
    return syscall6(7, 10, 20, 30, 40, 50, 60);
}

static long test_syscall_gamma(void)
{
    return syscall6(12, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF);
}

void _start()
{
    volatile long result_alpha = test_syscall_alpha();
    volatile long result_beta = test_syscall_beta();
    volatile long result_gamma = test_syscall_gamma();
    (void)result_alpha;
    (void)result_beta;
    (void)result_gamma;

    while (1)
    {
        __asm__ __volatile__("pause");
    }
}
