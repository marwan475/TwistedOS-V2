typedef unsigned long u64;

enum LinuxSyscallNumber
{
    LINUX_SYS_READ         = 0,
    LINUX_SYS_WRITE        = 1,
    LINUX_SYS_OPEN         = 2,
    LINUX_SYS_CLOSE        = 3,
    LINUX_SYS_EXIT         = 60,
};

enum LinuxOpenFlags
{
    LINUX_O_RDWR = 2,
};

static inline long syscall6(u64 number, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6)
{
    register u64 r10 __asm__("r10") = arg4;
    register u64 r8 __asm__("r8")   = arg5;
    register u64 r9 __asm__("r9")   = arg6;
    long         result;

    __asm__ __volatile__("syscall" : "=a"(result) : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");

    return result;
}

static inline long syscall2(u64 number, u64 arg1, u64 arg2)
{
    return syscall6(number, arg1, arg2, 0, 0, 0, 0);
}

static inline long syscall3(u64 number, u64 arg1, u64 arg2, u64 arg3)
{
    return syscall6(number, arg1, arg2, arg3, 0, 0, 0);
}

static long write_text(long fd, const char* text, u64 length)
{
    return syscall3(LINUX_SYS_WRITE, (u64) fd, (u64) text, length);
}

static long test_open_tty(void)
{
    static const char path[] = "/dev/tty";
    return syscall2(LINUX_SYS_OPEN, (u64) path, LINUX_O_RDWR);
}

static long test_close_file(long fd)
{
    return syscall6(LINUX_SYS_CLOSE, (u64) fd, 0, 0, 0, 0, 0);
}

static long test_read_file(long fd, void* buffer, u64 length)
{
    return syscall3(LINUX_SYS_READ, (u64) fd, (u64) buffer, length);
}

static long test_write_file(long fd, const void* buffer, u64 length)
{
    return syscall3(LINUX_SYS_WRITE, (u64) fd, (u64) buffer, length);
}

static __attribute__((noreturn)) void linux_exit(int code)
{
    (void) syscall6(LINUX_SYS_EXIT, (u64) code, 0, 0, 0, 0, 0);

    while (1)
    {
        __asm__ __volatile__("pause");
    }
}

void _start()
{
    volatile long tty_fd           = test_open_tty();
    volatile long tty_write_result = -1;
    volatile long tty_read_result  = -1;
    volatile long tty_close_result = -1;
    volatile long tty_double_close = -1;

    char tty_read_buffer[32] = {};

    static const char tty_open_success_message[]  = "open('/dev/tty') passed\n";
    static const char tty_write_probe[]           = "[tty write] init2 -> /dev/tty\n";
    static const char tty_write_success_message[] = "write('/dev/tty') passed\n";
    static const char tty_write_failure_message[] = "write('/dev/tty') failed\n";
    static const char tty_read_prompt[]           = "Type on keyboard now (read /dev/tty once)...\n";
    static const char tty_read_bytes_message[]    = "read('/dev/tty') returned >0\n";
    static const char tty_read_empty_message[]    = "read('/dev/tty') returned 0 (buffer empty)\n";
    static const char tty_read_failure_message[]  = "read('/dev/tty') failed\n";
    static const char tty_close_success_message[] = "close('/dev/tty') returned 0\n";
    static const char tty_close_failure_message[] = "close('/dev/tty') failed\n";
    static const char tty_ebadf_success_message[] = "close('/dev/tty' again) returned -EBADF\n";
    static const char tty_ebadf_failure_message[] = "close('/dev/tty' again) unexpected result\n";

    if (tty_fd >= 0)
    {
        (void) write_text(tty_fd, tty_open_success_message, sizeof(tty_open_success_message) - 1);

        tty_write_result = test_write_file(tty_fd, tty_write_probe, sizeof(tty_write_probe) - 1);
        if (tty_write_result == (long) (sizeof(tty_write_probe) - 1))
        {
            (void) write_text(tty_fd, tty_write_success_message, sizeof(tty_write_success_message) - 1);
        }
        else
        {
            (void) write_text(tty_fd, tty_write_failure_message, sizeof(tty_write_failure_message) - 1);
        }

        (void) write_text(tty_fd, tty_read_prompt, sizeof(tty_read_prompt) - 1);
        tty_read_result = test_read_file(tty_fd, tty_read_buffer, sizeof(tty_read_buffer));

        if (tty_read_result > 0)
        {
            (void) write_text(tty_fd, tty_read_bytes_message, sizeof(tty_read_bytes_message) - 1);
            (void) write_text(tty_fd, "[tty echo] ", sizeof("[tty echo] ") - 1);
            (void) write_text(tty_fd, tty_read_buffer, (u64) tty_read_result);
            (void) write_text(tty_fd, "\n", 1);
        }
        else if (tty_read_result == 0)
        {
            (void) write_text(tty_fd, tty_read_empty_message, sizeof(tty_read_empty_message) - 1);
        }
        else
        {
            (void) write_text(tty_fd, tty_read_failure_message, sizeof(tty_read_failure_message) - 1);
        }

        tty_close_result = test_close_file(tty_fd);
        tty_double_close = test_close_file(tty_fd);

        if (tty_close_result == 0)
        {
            (void) write_text(tty_fd, tty_close_success_message, sizeof(tty_close_success_message) - 1);
        }
        else
        {
            (void) write_text(tty_fd, tty_close_failure_message, sizeof(tty_close_failure_message) - 1);
        }

        if (tty_double_close == -9)
        {
            (void) write_text(tty_fd, tty_ebadf_success_message, sizeof(tty_ebadf_success_message) - 1);
        }
        else
        {
            (void) write_text(tty_fd, tty_ebadf_failure_message, sizeof(tty_ebadf_failure_message) - 1);
        }
    }

    (void) tty_fd;
    (void) tty_write_result;
    (void) tty_read_result;
    (void) tty_close_result;
    (void) tty_double_close;

    linux_exit(0);
}
