#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    static const char tty[]         = "/dev/tty";
    static const char shell[]       = "/bin/sh";
    static const char init_script[] = "/init.sh";
    static char* const shell_argv[] = {(char*) shell, (char*) init_script, 0};
    static char* const shell_envp[] = {0};

    int tty_fd = open(tty, O_RDWR);
    if (tty_fd >= 0)
    {
        dup2(tty_fd, STDIN_FILENO);
        dup2(tty_fd, STDOUT_FILENO);
        dup2(tty_fd, STDERR_FILENO);
        if (tty_fd > STDERR_FILENO)
        {
            close(tty_fd);
        }
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        execve(shell, shell_argv, shell_envp);
        _exit(127);
    }

    if (pid > 0)
    {
        int status = 0;
        waitpid(pid, &status, 0);
    }

    for (;;)
    {
        pause();
    }
}
