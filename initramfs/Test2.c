/**
 * File: Test2.c
 * Author: Marwan Mostafa
 * Description: User mode test process 2 - writes to TTY and stays running.
 */

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

void _start()
{
    static const char msg[] = "[UserModeTest] Test2 running\n";
    static const char tty[] = "/dev/tty";

    long tty_fd = syscall2(2 /* open */, (u64) tty, 2 /* O_RDWR */);
    if (tty_fd >= 0)
    {
        syscall3(1 /* write */, (u64) tty_fd, (u64) msg, sizeof(msg) - 1);
        syscall6(3 /* close */, (u64) tty_fd, 0, 0, 0, 0, 0);
    }

    /* Keep process alive to make scheduler behavior observable in the user-mode test window. */
    while (1)
    {
        __asm__ __volatile__("pause");
    }
}
