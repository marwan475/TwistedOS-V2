typedef unsigned long u64;

enum LinuxSyscallNumber
{
    LINUX_SYS_WRITE        = 1,
    LINUX_SYS_OPEN         = 2,
    LINUX_SYS_GETPID       = 39,
    LINUX_SYS_GETTID       = 186,
    LINUX_SYS_CLOCKGETTIME = 228,
    LINUX_SYS_EXIT         = 60,
};

enum LinuxOpenFlags
{
    LINUX_O_RDONLY = 0,
};

struct LinuxTimespec
{
    long tv_sec;
    long tv_nsec;
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

static inline long syscall0(u64 number)
{
    return syscall6(number, 0, 0, 0, 0, 0, 0);
}

static inline long syscall2(u64 number, u64 arg1, u64 arg2)
{
    return syscall6(number, arg1, arg2, 0, 0, 0, 0);
}

static inline long syscall3(u64 number, u64 arg1, u64 arg2, u64 arg3)
{
    return syscall6(number, arg1, arg2, arg3, 0, 0, 0);
}

static long test_write_banner(void)
{
    static const char message[] = "init2 linux static syscall smoke\n";
    return syscall3(LINUX_SYS_WRITE, 1, (u64) message, sizeof(message) - 1);
}

static long write_text(const char* text, u64 length)
{
    return syscall3(LINUX_SYS_WRITE, 1, (u64) text, length);
}

static long test_getpid(void)
{
    return syscall0(LINUX_SYS_GETPID);
}

static long test_gettid(void)
{
    return syscall0(LINUX_SYS_GETTID);
}

static long test_clock_gettime(void)
{
    struct LinuxTimespec ts;
    return syscall2(LINUX_SYS_CLOCKGETTIME, 0, (u64) &ts);
}

static long test_open_existing_file(void)
{
    static const char path[] = "/init2";
    return syscall2(LINUX_SYS_OPEN, (u64) path, LINUX_O_RDONLY);
}

static long test_open_missing_file(void)
{
    static const char path[] = "/no_such_file";
    return syscall2(LINUX_SYS_OPEN, (u64) path, LINUX_O_RDONLY);
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
    volatile long open_existing_fd    = test_open_existing_file();
    volatile long open_missing_result = test_open_missing_file();

    static const char open_success_message[] = "open('/init2') passed\n";
    static const char open_failure_message[] = "open('/init2') failed\n";
    static const char enoent_success_message[] = "open('/no_such_file') returned -ENOENT\n";
    static const char enoent_failure_message[] = "open('/no_such_file') unexpected result\n";

    if (open_existing_fd >= 0)
    {
        (void) write_text(open_success_message, sizeof(open_success_message) - 1);
    }
    else
    {
        (void) write_text(open_failure_message, sizeof(open_failure_message) - 1);
    }

    if (open_missing_result == -2)
    {
        (void) write_text(enoent_success_message, sizeof(enoent_success_message) - 1);
    }
    else
    {
        (void) write_text(enoent_failure_message, sizeof(enoent_failure_message) - 1);
    }

    (void) open_existing_fd;
    (void) open_missing_result;

    linux_exit(0);
}
