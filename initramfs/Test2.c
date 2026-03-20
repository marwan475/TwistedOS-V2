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

static u64 string_len(const char* text)
{
    if (text == 0)
    {
        return 0;
    }

    u64 length = 0;
    while (text[length] != '\0')
    {
        ++length;
    }

    return length;
}

static void write_text(long fd, const char* text)
{
    syscall3(1 /* write */, (u64) fd, (u64) text, string_len(text));
}

static void write_u64(long fd, u64 value)
{
    char buffer[32];
    u64  index = 0;

    if (value == 0)
    {
        buffer[index++] = '0';
    }
    else
    {
        while (value > 0 && index < sizeof(buffer))
        {
            buffer[index++] = (char) ('0' + (value % 10));
            value /= 10;
        }
    }

    for (u64 left = 0, right = (index == 0 ? 0 : index - 1); left < right; ++left, --right)
    {
        char temp     = buffer[left];
        buffer[left]  = buffer[right];
        buffer[right] = temp;
    }

    syscall3(1 /* write */, (u64) fd, (u64) buffer, index);
}

void _start(u64 argc, const char* const* argv, const char* const* envp)
{
    static const char msg[]          = "[UserModeTest] Test2 running\n";
    static const char argc_prefix[]  = "[UserModeTest] argc=";
    static const char argv_prefix[]  = "[UserModeTest] argv[";
    static const char envp_prefix[]  = "[UserModeTest] envp[";
    static const char index_suffix[] = "]=";
    static const char null_text[]    = "<null>";
    static const char newline[]      = "\n";
    static const char tty[]          = "/dev/tty";

    long tty_fd = syscall2(2 /* open */, (u64) tty, 2 /* O_RDWR */);
    if (tty_fd >= 0)
    {
        write_text(tty_fd, msg);
        write_text(tty_fd, argc_prefix);
        write_u64(tty_fd, argc);
        write_text(tty_fd, newline);

        for (u64 index = 0; index < argc; ++index)
        {
            write_text(tty_fd, argv_prefix);
            write_u64(tty_fd, index);
            write_text(tty_fd, index_suffix);
            if (argv != 0 && argv[index] != 0)
            {
                write_text(tty_fd, argv[index]);
            }
            else
            {
                write_text(tty_fd, null_text);
            }

            write_text(tty_fd, newline);
        }

        if (envp != 0)
        {
            for (u64 env_index = 0; envp[env_index] != 0; ++env_index)
            {
                write_text(tty_fd, envp_prefix);
                write_u64(tty_fd, env_index);
                write_text(tty_fd, index_suffix);
                write_text(tty_fd, envp[env_index]);
                write_text(tty_fd, newline);
            }
        }

        syscall6(3 /* close */, (u64) tty_fd, 0, 0, 0, 0, 0);
    }

    /* Keep process alive to make scheduler behavior observable in the user-mode test window. */
    while (1)
    {
        __asm__ __volatile__("pause");
    }
}
