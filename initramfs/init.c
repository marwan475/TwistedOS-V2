typedef unsigned long u64;

static inline long syscall6(u64 number, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6)
{
    register u64 r10 __asm__("r10") = arg4;
    register u64 r8 __asm__("r8")   = arg5;
    register u64 r9 __asm__("r9")   = arg6;
    long         result;

    __asm__ __volatile__("syscall" : "=a"(result) : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");

    return result;
}

static inline long syscall3(u64 number, u64 arg1, u64 arg2, u64 arg3)
{
    return syscall6(number, arg1, arg2, arg3, 0, 0, 0);
}

static inline long syscall2(u64 number, u64 arg1, u64 arg2)
{
    return syscall6(number, arg1, arg2, 0, 0, 0, 0);
}

static inline long syscall1(u64 number, u64 arg1)
{
    return syscall6(number, arg1, 0, 0, 0, 0, 0);
}

static inline long syscall0(u64 number)
{
    return syscall6(number, 0, 0, 0, 0, 0, 0);
}

void _start()
{
    static const char        tty[]        = "/dev/tty";
    static const char        shell[]      = "/bin/sh";
    static const char* const shell_argv[] = {shell, 0};
    static const char* const shell_envp[] = {0};

    long tty_fd = syscall2(2, (u64) tty, 2);
    if (tty_fd >= 0)
    {
        syscall2(33, (u64) tty_fd, 0);
        syscall2(33, (u64) tty_fd, 1);
        syscall2(33, (u64) tty_fd, 2);
        if (tty_fd > 2)
        {
            syscall1(3, (u64) tty_fd);
        }
    }

    long pid = syscall0(57);
    if (pid == 0)
    {
        syscall3(59, (u64) shell, (u64) shell_argv, (u64) shell_envp);
        syscall1(60, 127);
    }

    if (pid > 0)
    {
        int status = 0;
        syscall2(61, (u64) &status, 0);
    }

    for (;;)
    {
        __asm__ __volatile__("pause");
    }
}
