/**
 * File: POSIX_SystemCalls.cpp
 * Author: Marwan Mostafa
 * Description: Placeholder POSIX syscall-number switch for translation layer work.
 */

#include "TranslationLayer.hpp"

#include <Layers/Dispatcher.hpp>
#include <stdint.h>

/**
 * Function: TranslationLayer::HandlePosixSystemCallNumber
 * Description: Dispatches a POSIX/Linux syscall number to a placeholder switch case.
 * Parameters:
 *   uint64_t SystemCallNumber - POSIX/Linux syscall number to dispatch.
 *   uint64_t Arg1 - First syscall argument.
 *   uint64_t Arg2 - Second syscall argument.
 *   uint64_t Arg3 - Third syscall argument.
 *   uint64_t Arg4 - Fourth syscall argument.
 *   uint64_t Arg5 - Fifth syscall argument.
 *   uint64_t Arg6 - Sixth syscall argument.
 * Returns:
 *   void - Does not return a value.
 */

int64_t TranslationLayer::HandlePosixSystemCallNumber(uint64_t SystemCallNumber, uint64_t Arg1, uint64_t Arg2, uint64_t Arg3, uint64_t Arg4, uint64_t Arg5, uint64_t Arg6)
{
    (void) Arg5;
    (void) Arg6;

    constexpr int64_t LINUX_ERR_ENOSYS = -38;

    switch (SystemCallNumber)
    {
        case 0: // read
            return HandleReadSystemCall(Arg1, reinterpret_cast<void*>(Arg2), Arg3);
            break;
        case 1: // write
            return HandleWriteSystemCall(Arg1, reinterpret_cast<const void*>(Arg2), Arg3);
            break;
        case 2: // open
            return HandleOpenSystemCall(reinterpret_cast<const char*>(Arg1), Arg2);
            break;
        case 3: // close
            return HandleCloseSystemCall(Arg1);
            break;
        case 4: // stat
            return HandleStatSystemCall(reinterpret_cast<const char*>(Arg1), reinterpret_cast<void*>(Arg2));
            break;
        case 5: // fstat
            return HandleFstatSystemCall(Arg1, reinterpret_cast<void*>(Arg2));
            break;
        case 6: // lstat
            return HandleLstatSystemCall(reinterpret_cast<const char*>(Arg1), reinterpret_cast<void*>(Arg2));
            break;
        case 7: // poll
            return HandlePollSystemCall(reinterpret_cast<void*>(Arg1), Arg2, static_cast<int64_t>(Arg3));
            break;
        case 8: // lseek
            return HandleLseekSystemCall(Arg1, static_cast<int64_t>(Arg2), static_cast<int64_t>(Arg3));
            break;
        case 9: // mmap
            return HandleMmapSystemCall(reinterpret_cast<void*>(Arg1), Arg2, static_cast<int64_t>(Arg3), static_cast<int64_t>(Arg4), static_cast<int64_t>(Arg5), static_cast<int64_t>(Arg6));
            break;
        case 10: // mprotect
            return HandleMprotectSystemCall(reinterpret_cast<void*>(Arg1), Arg2, static_cast<int64_t>(Arg3));
            break;
        case 11: // munmap
            return HandleMunmapSystemCall(reinterpret_cast<void*>(Arg1), Arg2);
            break;
        case 12: // brk
            return HandleBrkSystemCall(Arg1);
            break;
        case 13: // rt_sigaction
            return HandleRtSigactionSystemCall(static_cast<int64_t>(Arg1), reinterpret_cast<const void*>(Arg2), reinterpret_cast<void*>(Arg3), Arg4);
            break;
        case 14: // rt_sigprocmask
            return HandleRtSigprocmaskSystemCall(static_cast<int64_t>(Arg1), reinterpret_cast<const void*>(Arg2), reinterpret_cast<void*>(Arg3), Arg4);
            break;
        case 16: // ioctl
            return HandleIoctlSystemCall(Arg1, Arg2, Arg3);
            break;
        case 19: // readv
            return HandleReadvSystemCall(Arg1, reinterpret_cast<const void*>(Arg2), Arg3);
            break;
        case 20: // writev
            return HandleWritevSystemCall(Arg1, reinterpret_cast<const void*>(Arg2), Arg3);
            break;
        case 21: // access
            return HandleAccessSystemCall(reinterpret_cast<const char*>(Arg1), static_cast<int64_t>(Arg2));
            break;
        case 22: // pipe
            return HandlePipeSystemCall(reinterpret_cast<void*>(Arg1));
            break;
        case 33: // dup2
            return HandleDup2SystemCall(Arg1, Arg2);
            break;
        case 34: // pause
            return HandlePauseSystemCall();
            break;
        case 35: // nanosleep
            return HandleNanosleepSystemCall(reinterpret_cast<const void*>(Arg1), reinterpret_cast<void*>(Arg2));
            break;
        case 38: // setitimer
            return HandleSetitimerSystemCall(static_cast<int64_t>(Arg1), reinterpret_cast<const void*>(Arg2), reinterpret_cast<void*>(Arg3));
            break;
        case 39: // getpid
            return HandleGetpidSystemCall();
            break;
        case 41: // socket
            return HandleSocketSystemCall(static_cast<int64_t>(Arg1), static_cast<int64_t>(Arg2), static_cast<int64_t>(Arg3));
            break;
        case 42: // connect
            return HandleConnectSystemCall(Arg1, reinterpret_cast<const void*>(Arg2), Arg3);
            break;
        case 43: // accept
            return HandleAcceptSystemCall(Arg1, reinterpret_cast<void*>(Arg2), reinterpret_cast<void*>(Arg3));
            break;
        case 48: // shutdown
            return HandleShutdownSystemCall(Arg1, static_cast<int64_t>(Arg2));
            break;
        case 49: // bind
            return HandleBindSystemCall(Arg1, reinterpret_cast<const void*>(Arg2), Arg3);
            break;
        case 50: // listen
            return HandleListenSystemCall(Arg1, static_cast<int64_t>(Arg2));
            break;
        case 51: // getsockname
            return HandleGetsocknameSystemCall(Arg1, reinterpret_cast<void*>(Arg2), reinterpret_cast<void*>(Arg3));
            break;
        case 54: // setsockopt
            return HandleSetsockoptSystemCall(Arg1, static_cast<int64_t>(Arg2), static_cast<int64_t>(Arg3), reinterpret_cast<const void*>(Arg4), Arg5);
            break;
        case 55: // getsockopt
            return HandleGetsockoptSystemCall(Arg1, static_cast<int64_t>(Arg2), static_cast<int64_t>(Arg3), reinterpret_cast<void*>(Arg4), reinterpret_cast<void*>(Arg5));
            break;
        case 57: // fork
            return HandleForkSystemCall();
            break;
        case 58: // vfork
            return HandleVforkSystemCall();
            break;
        case 59: // execve
            return HandleExecveSystemCall(reinterpret_cast<const char*>(Arg1), reinterpret_cast<const char* const*>(Arg2), reinterpret_cast<const char* const*>(Arg3));
            break;
        case 61: // wait4
            return HandleWaitSystemCall(static_cast<int64_t>(Arg1), reinterpret_cast<int*>(Arg2), static_cast<int64_t>(Arg3), reinterpret_cast<void*>(Arg4));
            break;
        case 62: // kill
            return HandleKillSystemCall(static_cast<int64_t>(Arg1), static_cast<int64_t>(Arg2));
            break;
        case 63: // uname
            return HandleUnameSystemCall(reinterpret_cast<void*>(Arg1));
            break;
        case 72: // fcntl
            return HandleFcntlSystemCall(Arg1, Arg2, Arg3);
            break;
        case 78: // getdents
            return HandleGetdents64SystemCall(Arg1, reinterpret_cast<void*>(Arg2), Arg3);
            break;
        case 79: // getcwd
            return HandleGetcwdSystemCall(reinterpret_cast<char*>(Arg1), Arg2);
            break;
        case 80: // chdir
            return HandleChdirSystemCall(reinterpret_cast<const char*>(Arg1));
            break;
        case 82: // rename
            return HandleRenameSystemCall(reinterpret_cast<const char*>(Arg1), reinterpret_cast<const char*>(Arg2));
            break;
        case 83: // mkdir
            return HandleMkdirSystemCall(reinterpret_cast<const char*>(Arg1), Arg2);
            break;
        case 84: // rmdir
            return HandleRmdirSystemCall(reinterpret_cast<const char*>(Arg1));
            break;
        case 86: // link
            return HandleLinkSystemCall(reinterpret_cast<const char*>(Arg1), reinterpret_cast<const char*>(Arg2));
            break;
        case 87: // unlink
            return HandleUnlinkSystemCall(reinterpret_cast<const char*>(Arg1));
            break;
        case 89: // readlink
            return HandleReadlinkSystemCall(reinterpret_cast<const char*>(Arg1), reinterpret_cast<char*>(Arg2), Arg3);
            break;
        case 90: // chmod
            return HandleChmodSystemCall(reinterpret_cast<const char*>(Arg1), Arg2);
            break;
        case 91: // fchmod
            return HandleFchmodSystemCall(Arg1, Arg2);
            break;
        case 95: // umask
            return HandleUmaskSystemCall(Arg1);
            break;
        case 96: // gettimeofday
            return HandleGettimeofdaySystemCall(reinterpret_cast<void*>(Arg1), reinterpret_cast<void*>(Arg2));
            break;
        case 97: // getrlimit
            return HandleGetrlimitSystemCall(static_cast<int64_t>(Arg1), reinterpret_cast<void*>(Arg2));
            break;
        case 102: // getuid
            return HandleGetuidSystemCall();
            break;
        case 105: // setuid
            return HandleSetuidSystemCall(static_cast<int64_t>(Arg1));
            break;
        case 104: // getgid
            return HandleGetgidSystemCall();
            break;
        case 106: // setgid
            return HandleSetgidSystemCall(static_cast<int64_t>(Arg1));
            break;
        case 107: // geteuid
            return HandleGeteuidSystemCall();
            break;
        case 108: // getegid
            return HandleGetegidSystemCall();
            break;
        case 109: // setpgid
            return HandleSetpgidSystemCall(static_cast<int64_t>(Arg1), static_cast<int64_t>(Arg2));
            break;
        case 110: // getppid
            return HandleGetppidSystemCall();
            break;
        case 111: // getpgrp
            return HandleGetpgrpSystemCall();
            break;
        case 112: // setsid
            return HandleSetsidSystemCall();
            break;
        case 132: // utime
            return HandleUtimeSystemCall(reinterpret_cast<const char*>(Arg1), reinterpret_cast<const void*>(Arg2));
            break;
        case 140: // getpriority
            return HandleGetprioritySystemCall(static_cast<int64_t>(Arg1), static_cast<int64_t>(Arg2));
            break;
        case 141: // setpriority
            return HandleSetprioritySystemCall(static_cast<int64_t>(Arg1), static_cast<int64_t>(Arg2), static_cast<int64_t>(Arg3));
            break;
        case 121: // getpgid
            return HandleGetpgidSystemCall(static_cast<int64_t>(Arg1));
            break;
        case 124: // getsid
            return HandleGetsidSystemCall(static_cast<int64_t>(Arg1));
            break;
        case 130: // rt_sigsuspend
            return HandleRtSigsuspendSystemCall(reinterpret_cast<const void*>(Arg1), Arg2);
            break;
        case 158: // arch_prctl
            return HandleArchPrctlSystemCall(Arg1, Arg2);
            break;
        case 161: // chroot
            return HandleChrootSystemCall(reinterpret_cast<const char*>(Arg1));
            break;
        case 165: // mount
            return HandleMountSystemCall(reinterpret_cast<const char*>(Arg1), reinterpret_cast<const char*>(Arg2), reinterpret_cast<const char*>(Arg3), Arg4, reinterpret_cast<const void*>(Arg5));
            break;
        case 217: // getdents64
            return HandleGetdents64SystemCall(Arg1, reinterpret_cast<void*>(Arg2), Arg3);
            break;
        case 218: // set_tid_address
            return HandleSetTidAddressSystemCall(reinterpret_cast<int*>(Arg1));
            break;
        case 228: // clock_gettime
            return HandleClockGettimeSystemCall(static_cast<int64_t>(Arg1), reinterpret_cast<void*>(Arg2));
            break;
        case 229: // clock_getres
            return HandleClockGetresSystemCall(static_cast<int64_t>(Arg1), reinterpret_cast<void*>(Arg2));
            break;
        case 231: // exit_group
            return HandleExitGroupSystemCall(static_cast<int64_t>(Arg1));
            break;
        case 233: // epoll_ctl
            return HandleEpollCtlSystemCall(Arg1, static_cast<int64_t>(Arg2), Arg3, reinterpret_cast<const void*>(Arg4));
            break;
        case 235: // utimes
            return HandleUtimesSystemCall(reinterpret_cast<const char*>(Arg1), reinterpret_cast<const void*>(Arg2));
            break;
        case 257: // openat
            return HandleOpenAtSystemCall(static_cast<int64_t>(Arg1), reinterpret_cast<const char*>(Arg2), Arg3, Arg4);
            break;
        case 261: // futimesat
            return HandleFutimesatSystemCall(static_cast<int64_t>(Arg1), reinterpret_cast<const char*>(Arg2), reinterpret_cast<const void*>(Arg3));
            break;
        case 262: // newfstatat
            return HandleNewFstatatSystemCall(static_cast<int64_t>(Arg1), reinterpret_cast<const char*>(Arg2), reinterpret_cast<void*>(Arg3), static_cast<int64_t>(Arg4));
            break;
        case 280: // utimensat
            return HandleUtimensatSystemCall(static_cast<int64_t>(Arg1), reinterpret_cast<const char*>(Arg2), reinterpret_cast<const void*>(Arg3), static_cast<int64_t>(Arg4));
            break;
        case 291: // epoll_create1
            return HandleEpollCreate1SystemCall(static_cast<int64_t>(Arg1));
            break;
        case 302: // prlimit64
            return HandlePrlimit64SystemCall(static_cast<int64_t>(Arg1), static_cast<int64_t>(Arg2), reinterpret_cast<const void*>(Arg3), reinterpret_cast<void*>(Arg4));
            break;
            /*
                    case 4: // stat
                        break;
                    case 5: // fstat
                        break;
                    case 6: // lstat
                        break;
                    case 7: // poll
                        break;
                    case 8: // lseek
                        break;
                    case 9: // mmap
                        break;
                    case 10: // mprotect
                        break;
                    case 11: // munmap
                        break;
                    case 12: // brk
                        break;
                    case 13: // rt_sigaction
                        break;
                    case 14: // rt_sigprocmask
                        break;
                    case 15: // rt_sigreturn
                        break;
                    case 16: // ioctl
                        break;
                    case 17: // pread64
                        break;
                    case 18: // pwrite64
                        break;
                    case 19: // readv
                        break;
                    case 20: // writev
                        break;
                    case 21: // access
                        break;
                    case 22: // pipe
                        break;
                    case 23: // select
                        break;
                    case 24: // sched_yield
                        break;
                    case 25: // mremap
                        break;
                    case 26: // msync
                        break;
                    case 27: // mincore
                        break;
                    case 28: // madvise
                        break;
                    case 29: // shmget
                        break;
                    case 30: // shmat
                        break;
                    case 31: // shmctl
                        break;
                    case 32: // dup
                        break;
                    case 33: // dup2
                        break;
                    case 34: // pause
                        break;
                    case 35: // nanosleep
                        break;
                    case 36: // getitimer
                        break;
                    case 37: // alarm
                        break;
                    case 38: // setitimer
                        break;
                    case 39: // getpid
                        break;
                    case 40: // sendfile
                        break;
                    case 41: // socket
                        break;
                    case 42: // connect
                        break;
                    case 43: // accept
                        break;
                    case 44: // sendto
                        break;
                    case 45: // recvfrom
                        break;
                    case 46: // sendmsg
                        break;
                    case 47: // recvmsg
                        break;
                    case 48: // shutdown
                        break;
                    case 49: // bind
                        break;
                    case 50: // listen
                        break;
                    case 51: // getsockname
                        break;
                    case 52: // getpeername
                        break;
                    case 53: // socketpair
                        break;
                    case 54: // setsockopt
                        break;
                    case 55: // getsockopt
                        break;
                    case 56: // clone
                        break;
                    case 57: // fork
                        break;
                    case 58: // vfork
                        break;
                    case 60: // exit
                        break;
                    case 61: // wait4
                        break;
                    case 62: // kill
                        break;
                    case 63: // uname
                        break;
                    case 64: // semget
                        break;
                    case 65: // semop
                        break;
                    case 66: // semctl
                        break;
                    case 67: // shmdt
                        break;
                    case 68: // msgget
                        break;
                    case 69: // msgsnd
                        break;
                    case 70: // msgrcv
                        break;
                    case 71: // msgctl
                        break;
                    case 72: // fcntl
                        break;
                    case 73: // flock
                        break;
                    case 74: // fsync
                        break;
                    case 75: // fdatasync
                        break;
                    case 76: // truncate
                        break;
                    case 77: // ftruncate
                        break;
                    case 78: // getdents
                        break;
                    case 79: // getcwd
                        break;
                    case 80: // chdir
                        break;
                    case 81: // fchdir
                        break;
                    case 82: // rename
                        break;
                    case 83: // mkdir
                        break;
                    case 84: // rmdir
                        break;
                    case 85: // creat
                        break;
                    case 86: // link
                        break;
                    case 87: // unlink
                        break;
                    case 88: // symlink
                        break;
                    case 89: // readlink
                        break;
                    case 90: // chmod
                        break;
                    case 91: // fchmod
                        break;
                    case 92: // chown
                        break;
                    case 93: // fchown
                        break;
                    case 94: // lchown
                        break;
                    case 95: // umask
                        break;
                    case 96: // gettimeofday
                        break;
                    case 97: // getrlimit
                        break;
                    case 98: // getrusage
                        break;
                    case 99: // sysinfo
                        break;
                    case 100: // times
                        break;
                    case 101: // ptrace
                        break;
                    case 102: // getuid
                        break;
                    case 103: // syslog
                        break;
                    case 104: // getgid
                        break;
                    case 105: // setuid
                        break;
                    case 106: // setgid
                        break;
                    case 107: // geteuid
                        break;
                    case 108: // getegid
                        break;
                    case 109: // setpgid
                        break;
                    case 110: // getppid
                        break;
                    case 111: // getpgrp
                        break;
                    case 112: // setsid
                        break;
                    case 113: // setreuid
                        break;
                    case 114: // setregid
                        break;
                    case 115: // getgroups
                        break;
                    case 116: // setgroups
                        break;
                    case 117: // setresuid
                        break;
                    case 118: // getresuid
                        break;
                    case 119: // setresgid
                        break;
                    case 120: // getresgid
                        break;
                    case 121: // getpgid
                        break;
                    case 122: // setfsuid
                        break;
                    case 123: // setfsgid
                        break;
                    case 124: // getsid
                        break;
                    case 125: // capget
                        break;
                    case 126: // capset
                        break;
                    case 127: // rt_sigpending
                        break;
                    case 128: // rt_sigtimedwait
                        break;
                    case 129: // rt_sigqueueinfo
                        break;
                    case 130: // rt_sigsuspend
                        break;
                    case 131: // sigaltstack
                        break;
                    case 132: // utime
                        break;
                    case 133: // mknod
                        break;
                    case 134: // uselib
                        break;
                    case 135: // personality
                        break;
                    case 136: // ustat
                        break;
                    case 137: // statfs
                        break;
                    case 138: // fstatfs
                        break;
                    case 139: // sysfs
                        break;
                    case 140: // getpriority
                        break;
                    case 141: // setpriority
                        break;
                    case 142: // sched_setparam
                        break;
                    case 143: // sched_getparam
                        break;
                    case 144: // sched_setscheduler
                        break;
                    case 145: // sched_getscheduler
                        break;
                    case 146: // sched_get_priority_max
                        break;
                    case 147: // sched_get_priority_min
                        break;
                    case 148: // sched_rr_get_interval
                        break;
                    case 149: // mlock
                        break;
                    case 150: // munlock
                        break;
                    case 151: // mlockall
                        break;
                    case 152: // munlockall
                        break;
                    case 153: // vhangup
                        break;
                    case 154: // modify_ldt
                        break;
                    case 155: // pivot_root
                        break;
                    case 156: // _sysctl
                        break;
                    case 157: // prctl
                        break;
                    case 158: // arch_prctl
                        break;
                    case 159: // adjtimex
                        break;
                    case 160: // setrlimit
                        break;
                    case 161: // chroot
                        break;
                    case 162: // sync
                        break;
                    case 163: // acct
                        break;
                    case 164: // settimeofday
                        break;
                    case 165: // mount
                        break;
                    case 166: // umount2
                        break;
                    case 167: // swapon
                        break;
                    case 168: // swapoff
                        break;
                    case 169: // reboot
                        break;
                    case 170: // sethostname
                        break;
                    case 171: // setdomainname
                        break;
                    case 172: // iopl
                        break;
                    case 173: // ioperm
                        break;
                    case 174: // create_module
                        break;
                    case 175: // init_module
                        break;
                    case 176: // delete_module
                        break;
                    case 177: // get_kernel_syms
                        break;
                    case 178: // query_module
                        break;
                    case 179: // quotactl
                        break;
                    case 180: // nfsservctl
                        break;
                    case 181: // getpmsg
                        break;
                    case 182: // putpmsg
                        break;
                    case 183: // afs_syscall
                        break;
                    case 184: // tuxcall
                        break;
                    case 185: // security
                        break;
                    case 186: // gettid
                        break;
                    case 187: // readahead
                        break;
                    case 188: // setxattr
                        break;
                    case 189: // lsetxattr
                        break;
                    case 190: // fsetxattr
                        break;
                    case 191: // getxattr
                        break;
                    case 192: // lgetxattr
                        break;
                    case 193: // fgetxattr
                        break;
                    case 194: // listxattr
                        break;
                    case 195: // llistxattr
                        break;
                    case 196: // flistxattr
                        break;
                    case 197: // removexattr
                        break;
                    case 198: // lremovexattr
                        break;
                    case 199: // fremovexattr
                        break;
                    case 200: // tkill
                        break;
                    case 201: // time
                        break;
                    case 202: // futex
                        break;
                    case 203: // sched_setaffinity
                        break;
                    case 204: // sched_getaffinity
                        break;
                    case 205: // set_thread_area
                        break;
                    case 206: // io_setup
                        break;
                    case 207: // io_destroy
                        break;
                    case 208: // io_getevents
                        break;
                    case 209: // io_submit
                        break;
                    case 210: // io_cancel
                        break;
                    case 211: // get_thread_area
                        break;
                    case 212: // lookup_dcookie
                        break;
                    case 213: // epoll_create
                        break;
                    case 214: // epoll_ctl_old
                        break;
                    case 215: // epoll_wait_old
                        break;
                    case 216: // remap_file_pages
                        break;
                    case 217: // getdents64
                        break;
                    case 218: // set_tid_address
                        break;
                    case 219: // restart_syscall
                        break;
                    case 220: // semtimedop
                        break;
                    case 221: // fadvise64
                        break;
                    case 222: // timer_create
                        break;
                    case 223: // timer_settime
                        break;
                    case 224: // timer_gettime
                        break;
                    case 225: // timer_getoverrun
                        break;
                    case 226: // timer_delete
                        break;
                    case 227: // clock_settime
                        break;
                    case 228: // clock_gettime
                        break;
                    case 229: // clock_getres
                        break;
                    case 230: // clock_nanosleep
                        break;
                    case 231: // exit_group
                        break;
                    case 232: // epoll_wait
                        break;
                    case 233: // epoll_ctl
                        break;
                    case 234: // tgkill
                        break;
                    case 235: // utimes
                        break;
                    case 236: // vserver
                        break;
                    case 237: // mbind
                        break;
                    case 238: // set_mempolicy
                        break;
                    case 239: // get_mempolicy
                        break;
                    case 240: // mq_open
                        break;
                    case 241: // mq_unlink
                        break;
                    case 242: // mq_timedsend
                        break;
                    case 243: // mq_timedreceive
                        break;
                    case 244: // mq_notify
                        break;
                    case 245: // mq_getsetattr
                        break;
                    case 246: // kexec_load
                        break;
                    case 247: // waitid
                        break;
                    case 248: // add_key
                        break;
                    case 249: // request_key
                        break;
                    case 250: // keyctl
                        break;
                    case 251: // ioprio_set
                        break;
                    case 252: // ioprio_get
                        break;
                    case 253: // inotify_init
                        break;
                    case 254: // inotify_add_watch
                        break;
                    case 255: // inotify_rm_watch
                        break;
                    case 256: // migrate_pages
                        break;
                    case 257: // openat
                        break;
                    case 258: // mkdirat
                        break;
                    case 259: // mknodat
                        break;
                    case 260: // fchownat
                        break;
                    case 261: // futimesat
                        break;
                    case 262: // newfstatat
                        break;
                    case 263: // unlinkat
                        break;
                    case 264: // renameat
                        break;
                    case 265: // linkat
                        break;
                    case 266: // symlinkat
                        break;
                    case 267: // readlinkat
                        break;
                    case 268: // fchmodat
                        break;
                    case 269: // faccessat
                        break;
                    case 270: // pselect6
                        break;
                    case 271: // ppoll
                        break;
                    case 272: // unshare
                        break;
                    case 273: // set_robust_list
                        break;
                    case 274: // get_robust_list
                        break;
                    case 275: // splice
                        break;
                    case 276: // tee
                        break;
                    case 277: // sync_file_range
                        break;
                    case 278: // vmsplice
                        break;
                    case 279: // move_pages
                        break;
                    case 280: // utimensat
                        break;
                    case 281: // epoll_pwait
                        break;
                    case 282: // signalfd
                        break;
                    case 283: // timerfd_create
                        break;
                    case 284: // eventfd
                        break;
                    case 285: // fallocate
                        break;
                    case 286: // timerfd_settime
                        break;
                    case 287: // timerfd_gettime
                        break;
                    case 288: // accept4
                        break;
                    case 289: // signalfd4
                        break;
                    case 290: // eventfd2
                        break;
                    case 292: // dup3
                        break;
                    case 293: // pipe2
                        break;
                    case 294: // inotify_init1
                        break;
                    case 295: // preadv
                        break;
                    case 296: // pwritev
                        break;
                    case 297: // rt_tgsigqueueinfo
                        break;
                    case 298: // perf_event_open
                        break;
                    case 299: // recvmmsg
                        break;
                    case 300: // fanotify_init
                        break;
                    case 301: // fanotify_mark
                        break;
                    case 302: // prlimit64
                        break;
                    case 303: // name_to_handle_at
                        break;
                    case 304: // open_by_handle_at
                        break;
                    case 305: // clock_adjtime
                        break;
                    case 306: // syncfs
                        break;
                    case 307: // sendmmsg
                        break;
                    case 308: // setns
                        break;
                    case 309: // getcpu
                        break;
                    case 310: // process_vm_readv
                        break;
                    case 311: // process_vm_writev
                        break;
                    case 312: // kcmp
                        break;
                    case 313: // finit_module
                        break;
                    case 314: // sched_setattr
                        break;
                    case 315: // sched_getattr
                        break;
                    case 316: // renameat2
                        break;
                    case 317: // seccomp
                        break;
                    case 318: // getrandom
                        break;
                    case 319: // memfd_create
                        break;
                    case 320: // kexec_file_load
                        break;
                    case 321: // bpf
                        break;
                    case 322: // execveat
                        break;
                    case 323: // userfaultfd
                        break;
                    case 324: // membarrier
                        break;
                    case 325: // mlock2
                        break;
                    case 326: // copy_file_range
                        break;
                    case 327: // preadv2
                        break;
                    case 328: // pwritev2
                        break;
                    case 329: // pkey_mprotect
                        break;
                    case 330: // pkey_alloc
                        break;
                    case 331: // pkey_free
                        break;
                    case 332: // statx
                        break;
                    case 333: // io_pgetevents
                        break;
                    case 334: // rseq
                        break;
                    case 335: // reserved/unused
                        break;
                    case 336: // reserved/unused
                        break;
                    case 337: // reserved/unused
                        break;
                    case 338: // reserved/unused
                        break;
                    case 339: // reserved/unused
                        break;
                    case 340: // reserved/unused
                        break;
                    case 341: // reserved/unused
                        break;
                    case 342: // reserved/unused
                        break;
                    case 343: // reserved/unused
                        break;
                    case 344: // reserved/unused
                        break;
                    case 345: // reserved/unused
                        break;
                    case 346: // reserved/unused
                        break;
                    case 347: // reserved/unused
                        break;
                    case 348: // reserved/unused
                        break;
                    case 349: // reserved/unused
                        break;
                    case 350: // reserved/unused
                        break;
                    case 351: // reserved/unused
                        break;
                    case 352: // reserved/unused
                        break;
                    case 353: // reserved/unused
                        break;
                    case 354: // reserved/unused
                        break;
                    case 355: // reserved/unused
                        break;
                    case 356: // reserved/unused
                        break;
                    case 357: // reserved/unused
                        break;
                    case 358: // reserved/unused
                        break;
                    case 359: // reserved/unused
                        break;
                    case 360: // reserved/unused
                        break;
                    case 361: // reserved/unused
                        break;
                    case 362: // reserved/unused
                        break;
                    case 363: // reserved/unused
                        break;
                    case 364: // reserved/unused
                        break;
                    case 365: // reserved/unused
                        break;
                    case 366: // reserved/unused
                        break;
                    case 367: // reserved/unused
                        break;
                    case 368: // reserved/unused
                        break;
                    case 369: // reserved/unused
                        break;
                    case 370: // reserved/unused
                        break;
                    case 371: // reserved/unused
                        break;
                    case 372: // reserved/unused
                        break;
                    case 373: // reserved/unused
                        break;
                    case 374: // reserved/unused
                        break;
                    case 375: // reserved/unused
                        break;
                    case 376: // reserved/unused
                        break;
                    case 377: // reserved/unused
                        break;
                    case 378: // reserved/unused
                        break;
                    case 379: // reserved/unused
                        break;
                    case 380: // reserved/unused
                        break;
                    case 381: // reserved/unused
                        break;
                    case 382: // reserved/unused
                        break;
                    case 383: // reserved/unused
                        break;
                    case 384: // reserved/unused
                        break;
                    case 385: // reserved/unused
                        break;
                    case 386: // reserved/unused
                        break;
                    case 387: // reserved/unused
                        break;
                    case 388: // reserved/unused
                        break;
                    case 389: // reserved/unused
                        break;
                    case 390: // reserved/unused
                        break;
                    case 391: // reserved/unused
                        break;
                    case 392: // reserved/unused
                        break;
                    case 393: // reserved/unused
                        break;
                    case 394: // reserved/unused
                        break;
                    case 395: // reserved/unused
                        break;
                    case 396: // reserved/unused
                        break;
                    case 397: // reserved/unused
                        break;
                    case 398: // reserved/unused
                        break;
                    case 399: // reserved/unused
                        break;
                    case 400: // reserved/unused
                        break;
                    case 401: // reserved/unused
                        break;
                    case 402: // reserved/unused
                        break;
                    case 403: // reserved/unused
                        break;
                    case 404: // reserved/unused
                        break;
                    case 405: // reserved/unused
                        break;
                    case 406: // reserved/unused
                        break;
                    case 407: // reserved/unused
                        break;
                    case 408: // reserved/unused
                        break;
                    case 409: // reserved/unused
                        break;
                    case 410: // reserved/unused
                        break;
                    case 411: // reserved/unused
                        break;
                    case 412: // reserved/unused
                        break;
                    case 413: // reserved/unused
                        break;
                    case 414: // reserved/unused
                        break;
                    case 415: // reserved/unused
                        break;
                    case 416: // reserved/unused
                        break;
                    case 417: // reserved/unused
                        break;
                    case 418: // reserved/unused
                        break;
                    case 419: // reserved/unused
                        break;
                    case 420: // reserved/unused
                        break;
                    case 421: // reserved/unused
                        break;
                    case 422: // reserved/unused
                        break;
                    case 423: // reserved/unused
                        break;
                    case 424: // pidfd_send_signal
                        break;
                    case 425: // io_uring_setup
                        break;
                    case 426: // io_uring_enter
                        break;
                    case 427: // io_uring_register
                        break;
                    case 428: // open_tree
                        break;
                    case 429: // move_mount
                        break;
                    case 430: // fsopen
                        break;
                    case 431: // fsconfig
                        break;
                    case 432: // fsmount
                        break;
                    case 433: // fspick
                        break;
                    case 434: // pidfd_open
                        break;
                    case 435: // clone3
                        break;
                    case 436: // close_range
                        break;
                    case 437: // openat2
                        break;
                    case 438: // pidfd_getfd
                        break;
                    case 439: // faccessat2
                        break;
                    case 440: // process_madvise
                        break;
                    case 441: // epoll_pwait2
                        break;
                    case 442: // mount_setattr
                        break;
                    case 443: // quotactl_fd
                        break;
                    case 444: // landlock_create_ruleset
                        break;
                    case 445: // landlock_add_rule
                        break;
                    case 446: // landlock_restrict_self
                        break;
                    case 447: // memfd_secret
                        break;
                    case 448: // process_mrelease
                        break;
                    case 449: // futex_waitv
                        break;
                    case 450: // set_mempolicy_home_node
                        break;
                    case 451: // cachestat
                        break;
            */

        default:
        {
            Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
            if (ActiveDispatcher != nullptr)
            {
                ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("Unknown system call: %d\n", (int) SystemCallNumber);
            }
            return LINUX_ERR_ENOSYS;
        }
        break;
    }

    return LINUX_ERR_ENOSYS;
}
