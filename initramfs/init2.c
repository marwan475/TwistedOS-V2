typedef unsigned long u64;

enum LinuxSyscallNumber
{
    LINUX_SYS_WRITE        = 1,
    LINUX_SYS_GETPID       = 39,
    LINUX_SYS_GETTID       = 186,
    LINUX_SYS_CLOCKGETTIME = 228,
    LINUX_SYS_EXIT         = 60,
};

struct LinuxTimespec
{
    long tv_sec;
    long tv_nsec;
};

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
    return syscall3(LINUX_SYS_WRITE, 1, (u64)message, sizeof(message) - 1);
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
    return syscall2(LINUX_SYS_CLOCKGETTIME, 0, (u64)&ts);
}

static __attribute__((noreturn)) void linux_exit(int code)
{
    (void)syscall6(LINUX_SYS_EXIT, (u64)code, 0, 0, 0, 0, 0);

    while (1)
    {
        __asm__ __volatile__("pause");
    }
}

void _start()
{
    volatile long write_result = test_write_banner();
    volatile long pid_result   = test_getpid();
    volatile long tid_result   = test_gettid();
    volatile long time_result  = test_clock_gettime();

    (void)write_result;
    (void)pid_result;
    (void)tid_result;
    (void)time_result;

    linux_exit(0);
}
