/**
 * File: Test1.c
 * Author: Marwan Mostafa
 * Description: User mode test process 1 - writes to TTY then execves into Test2.
 */

typedef unsigned long u64;

static inline long syscall6(u64 number, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6)
{
    register u64 r10 __asm__("r10") = arg4;
    register u64 r8  __asm__("r8")  = arg5;
    register u64 r9  __asm__("r9")  = arg6;
    long result;

    __asm__ __volatile__("syscall"
                         : "=a"(result)
                         : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");

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
    static const char msg[]  = "[UserModeTest] Test1 running, execve -> /Test2\n";
    static const char tty[]  = "/dev/tty";
    static const char next[] = "/Test2";

    long tty_fd = syscall2(2 /* open */, (u64) tty, 2 /* O_RDWR */);
    if (tty_fd >= 0)
    {
        syscall3(1 /* write */, (u64) tty_fd, (u64) msg, sizeof(msg) - 1);
        syscall6(3 /* close */, (u64) tty_fd, 0, 0, 0, 0, 0);
    }

    /* Replace this process image with Test2; the loop continues via execve chaining. */
    syscall3(59 /* execve */, (u64) next, 0, 0);

    /* execve failed - spin to avoid returning off the end of the binary. */
    while (1)
    {
        __asm__ __volatile__("pause");
    }
}
