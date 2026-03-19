/**
 * File: Test1.c
 * Author: Marwan Mostafa
 * Description: User mode test process 1 - forks and has the child execve into Test2.
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
    static const char start_msg[]  = "[UserModeTest] Test1 start: calling fork\n";
    static const char parent_msg[] = "[UserModeTest] Test1 parent running after fork\n";
    static const char wait_msg[]   = "[UserModeTest] Test1 parent waiting for child\n";
    static const char waited_msg[] = "[UserModeTest] Test1 parent wait returned\n";
    static const char wait_err[]   = "[UserModeTest] Test1 parent wait failed\n";
    static const char child_msg[]  = "[UserModeTest] Test1 child execve -> /Test2\n";
    static const char fail_msg[]   = "[UserModeTest] Test1 child execve failed\n";
    static const char fork_err[]   = "[UserModeTest] Test1 fork failed\n";
    static const char tty[]        = "/dev/tty";
    static const char next[]       = "/Test2";

    long start_tty_fd = syscall2(2 /* open */, (u64) tty, 2 /* O_RDWR */);
    if (start_tty_fd >= 0)
    {
        syscall3(1 /* write */, (u64) start_tty_fd, (u64) start_msg, sizeof(start_msg) - 1);
        syscall6(3 /* close */, (u64) start_tty_fd, 0, 0, 0, 0, 0);
    }

    long pid = syscall6(57 /* fork */, 0, 0, 0, 0, 0, 0);

    if (pid == 0)
    {
        long child_tty_fd = syscall2(2 /* open */, (u64) tty, 2 /* O_RDWR */);
        if (child_tty_fd >= 0)
        {
            syscall3(1 /* write */, (u64) child_tty_fd, (u64) child_msg, sizeof(child_msg) - 1);
            syscall6(3 /* close */, (u64) child_tty_fd, 0, 0, 0, 0, 0);
        }

        syscall3(59 /* execve */, (u64) next, 0, 0);

        long fail_tty_fd = syscall2(2 /* open */, (u64) tty, 2 /* O_RDWR */);
        if (fail_tty_fd >= 0)
        {
            syscall3(1 /* write */, (u64) fail_tty_fd, (u64) fail_msg, sizeof(fail_msg) - 1);
            syscall6(3 /* close */, (u64) fail_tty_fd, 0, 0, 0, 0, 0);
        }
    }
    else if (pid > 0)
    {
        int child_status = 0;

        long parent_tty_fd = syscall2(2 /* open */, (u64) tty, 2 /* O_RDWR */);
        if (parent_tty_fd >= 0)
        {
            syscall3(1 /* write */, (u64) parent_tty_fd, (u64) parent_msg, sizeof(parent_msg) - 1);
            syscall3(1 /* write */, (u64) parent_tty_fd, (u64) wait_msg, sizeof(wait_msg) - 1);
            syscall6(3 /* close */, (u64) parent_tty_fd, 0, 0, 0, 0, 0);
        }

        long waited_pid = syscall6(61 /* wait */, (u64) &child_status, 0, 0, 0, 0, 0);

        long wait_tty_fd = syscall2(2 /* open */, (u64) tty, 2 /* O_RDWR */);
        if (wait_tty_fd >= 0)
        {
            if (waited_pid >= 0)
            {
                syscall3(1 /* write */, (u64) wait_tty_fd, (u64) waited_msg, sizeof(waited_msg) - 1);
            }
            else
            {
                syscall3(1 /* write */, (u64) wait_tty_fd, (u64) wait_err, sizeof(wait_err) - 1);
            }

            syscall6(3 /* close */, (u64) wait_tty_fd, 0, 0, 0, 0, 0);
        }
    }
    else
    {
        long error_tty_fd = syscall2(2 /* open */, (u64) tty, 2 /* O_RDWR */);
        if (error_tty_fd >= 0)
        {
            syscall3(1 /* write */, (u64) error_tty_fd, (u64) fork_err, sizeof(fork_err) - 1);
            syscall6(3 /* close */, (u64) error_tty_fd, 0, 0, 0, 0, 0);
        }
    }

    /* Keep process alive under the scheduler if fork/execve fails or for parent side validation. */
    while (1)
    {
        __asm__ __volatile__("pause");
    }
}
