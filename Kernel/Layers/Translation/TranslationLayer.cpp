/**
 * File: TranslationLayer.cpp
 * Author: Marwan Mostafa
 * Description: Translation layer implementation between system layers.
 */

#include "TranslationLayer.hpp"

#include "Layers/Logic/LogicLayer.hpp"

#include <Arch/x86.hpp>
#include <CommonUtils.hpp>
#include <Layers/Dispatcher.hpp>
#include <Layers/Resource/ResourceLayer.hpp>
#include <Layers/Resource/TTY.hpp>
#include <Layers/Resource/VirtualAddressSpace.hpp>
#include <Memory/VirtualMemoryManager.hpp>

namespace
{
constexpr int64_t LINUX_ERR_EFAULT    = -14;
constexpr int64_t LINUX_ERR_ENOENT    = -2;
constexpr int64_t LINUX_ERR_ENOMEM    = -12;
constexpr int64_t LINUX_ERR_EAGAIN    = -11;
constexpr int64_t LINUX_ERR_EMFILE    = -24;
constexpr int64_t LINUX_ERR_EINVAL    = -22;
constexpr int64_t LINUX_ERR_EBADF     = -9;
constexpr int64_t LINUX_ERR_EINTR     = -4;
constexpr int64_t LINUX_ERR_ESRCH     = -3;
constexpr int64_t LINUX_ERR_ENOSYS    = -38;
constexpr int64_t LINUX_ERR_ENOTTY    = -25;
constexpr int64_t LINUX_ERR_EACCES    = -13;
constexpr int64_t LINUX_ERR_ECHILD    = -10;
constexpr int64_t LINUX_ERR_ENODEV    = -19;
constexpr int64_t LINUX_ERR_ENOTDIR   = -20;
constexpr int64_t LINUX_ERR_EISDIR    = -21;
constexpr int64_t LINUX_ERR_EPERM     = -1;
constexpr int64_t LINUX_ERR_ERANGE    = -34;
constexpr int64_t LINUX_ERR_EEXIST    = -17;
constexpr int64_t LINUX_ERR_ENOTEMPTY = -39;
constexpr int64_t LINUX_ERR_ESPIPE    = -29;
constexpr int64_t LINUX_ERR_ENOTSOCK  = -88;
constexpr int64_t LINUX_ERR_ENOTCONN  = -107;
constexpr int64_t LINUX_ERR_EAFNOSUPPORT = -97;
constexpr int64_t LINUX_ERR_ESOCKTNOSUPPORT = -94;
constexpr int64_t LINUX_ERR_ENOPROTOOPT = -92;

constexpr uint64_t SYSCALL_COPY_CHUNK_SIZE   = 4096;
constexpr uint64_t SYSCALL_PATH_MAX          = 4096;
constexpr uint64_t SYSCALL_EXEC_MAX_VECTOR   = 128;
constexpr uint64_t SYSCALL_MAX_PATH_SEGMENTS = 256;
constexpr uint64_t SYSCALL_IOV_MAX           = 1024;
constexpr uint64_t SYSCALL_MAX_SOCKET_ADDRESS = 256;

struct LinuxIOVec
{
    uint64_t Base;
    uint64_t Length;
};

struct LinuxStat
{
    uint64_t Device;
    uint64_t Inode;
    uint64_t HardLinkCount;
    uint32_t Mode;
    uint32_t UserId;
    uint32_t GroupId;
    uint32_t Padding0;
    uint64_t SpecialDevice;
    int64_t  Size;
    int64_t  BlockSize;
    int64_t  Blocks;
    uint64_t AccessTimeSeconds;
    uint64_t AccessTimeNanoseconds;
    uint64_t ModifyTimeSeconds;
    uint64_t ModifyTimeNanoseconds;
    uint64_t ChangeTimeSeconds;
    uint64_t ChangeTimeNanoseconds;
    int64_t  Reserved[3];
};

struct LinuxTimeVal
{
    int64_t Seconds;
    int64_t Microseconds;
};

struct LinuxITimerVal
{
    LinuxTimeVal Interval;
    LinuxTimeVal Value;
};

struct LinuxUTimeBuf
{
    int64_t AccessTime;
    int64_t ModifyTime;
};

struct LinuxTimeSpec
{
    int64_t Seconds;
    int64_t Nanoseconds;
};

struct LinuxRLimit64
{
    uint64_t Current;
    uint64_t Maximum;
};

struct LinuxPollFd
{
    int32_t FileDescriptor;
    int16_t Events;
    int16_t Revents;
};

struct LinuxEpollEvent
{
    uint32_t Events;
    uint64_t Data;
} __attribute__((packed));

struct LinuxUtsName
{
    char SysName[65];
    char NodeName[65];
    char Release[65];
    char Version[65];
    char Machine[65];
    char DomainName[65];
};

constexpr int64_t LINUX_UTIME_NOW  = 1073741823;
constexpr int64_t LINUX_UTIME_OMIT = 1073741822;

constexpr uint32_t LINUX_S_IFREG = 0100000;
constexpr uint32_t LINUX_S_IFDIR = 0040000;
constexpr uint32_t LINUX_S_IFLNK = 0120000;
constexpr uint32_t LINUX_S_IFCHR = 0020000;

constexpr uint32_t LINUX_DEFAULT_FILE_PERMISSIONS = 0755;
constexpr uint32_t LINUX_DEFAULT_DIR_PERMISSIONS  = 0755;
constexpr uint32_t LINUX_DEFAULT_LINK_PERMISSIONS = 0777;
constexpr uint32_t LINUX_DEFAULT_CHAR_PERMISSIONS = 0666;

constexpr int64_t LINUX_MAP_SHARED    = 0x01;
constexpr int64_t LINUX_MAP_PRIVATE   = 0x02;
constexpr int64_t LINUX_MAP_FIXED     = 0x10;
constexpr int64_t LINUX_MAP_ANONYMOUS = 0x20;

constexpr int64_t LINUX_PROT_READ  = 0x1;
constexpr int64_t LINUX_PROT_WRITE = 0x2;
constexpr int64_t LINUX_PROT_EXEC  = 0x4;
constexpr int64_t LINUX_PROT_NONE  = 0x0;

constexpr uint64_t MMAP_DEFAULT_BASE = 0x0000000001000000;
constexpr uint64_t MMAP_HIGH_BASE    = 0x0000000100000000;

constexpr uint64_t LINUX_O_ACCMODE  = 0x3;
constexpr uint64_t LINUX_O_CREAT    = 0x40;
constexpr uint64_t LINUX_O_EXCL     = 0x80;
constexpr uint64_t LINUX_O_APPEND   = 0x400;
constexpr uint64_t LINUX_O_NONBLOCK = 0x800;
constexpr uint64_t LINUX_O_ASYNC    = 0x2000;
constexpr uint64_t LINUX_O_DIRECT   = 0x4000;
constexpr uint64_t LINUX_O_NOATIME  = 0x40000;
constexpr uint64_t LINUX_O_CLOEXEC  = 0x80000;

constexpr int64_t LINUX_SOCKET_CALL_TYPE_MASK = 0xF;
constexpr int64_t LINUX_SOCKET_CALL_NONBLOCK  = static_cast<int64_t>(LINUX_O_NONBLOCK);
constexpr int64_t LINUX_SOCKET_CALL_CLOEXEC   = static_cast<int64_t>(LINUX_O_CLOEXEC);

constexpr int64_t LINUX_SOL_SOCKET    = 1;
constexpr int64_t LINUX_SO_REUSEADDR  = 2;
constexpr int64_t LINUX_SO_TYPE       = 3;
constexpr int64_t LINUX_SO_ERROR      = 4;
constexpr int64_t LINUX_SO_BROADCAST  = 6;
constexpr int64_t LINUX_SO_SNDBUF     = 7;
constexpr int64_t LINUX_SO_RCVBUF     = 8;
constexpr int64_t LINUX_SO_KEEPALIVE  = 9;
constexpr int64_t LINUX_SO_OOBINLINE  = 10;
constexpr int64_t LINUX_SO_REUSEPORT  = 15;
constexpr int64_t LINUX_SO_PASSCRED   = 16;
constexpr int64_t LINUX_SO_RCVLOWAT   = 18;
constexpr int64_t LINUX_SO_SNDLOWAT   = 19;
constexpr int64_t LINUX_SO_RCVTIMEO   = 20;
constexpr int64_t LINUX_SO_SNDTIMEO   = 21;
constexpr int64_t LINUX_SO_BINDTODEVICE = 25;
constexpr int64_t LINUX_SO_ACCEPTCONN = 30;
constexpr int64_t LINUX_SO_PROTOCOL   = 38;
constexpr int64_t LINUX_SO_DOMAIN     = 39;

constexpr int64_t LINUX_ACCESS_F_OK = 0;
constexpr int64_t LINUX_ACCESS_X_OK = 1;
constexpr int64_t LINUX_ACCESS_W_OK = 2;
constexpr int64_t LINUX_ACCESS_R_OK = 4;

constexpr uint64_t LINUX_F_DUPFD         = 0;
constexpr uint64_t LINUX_F_GETFD         = 1;
constexpr uint64_t LINUX_F_SETFD         = 2;
constexpr uint64_t LINUX_F_GETFL         = 3;
constexpr uint64_t LINUX_F_SETFL         = 4;
constexpr uint64_t LINUX_F_DUPFD_CLOEXEC = 1030;
constexpr uint64_t LINUX_FD_CLOEXEC      = 0x1;

constexpr uint64_t LINUX_FCNTL_SETFL_ALLOWED = (LINUX_O_APPEND | LINUX_O_NONBLOCK | LINUX_O_ASYNC | LINUX_O_DIRECT | LINUX_O_NOATIME);

constexpr int32_t LINUX_SEEK_SET = 0;
constexpr int32_t LINUX_SEEK_CUR = 1;
constexpr int32_t LINUX_SEEK_END = 2;

constexpr int16_t LINUX_POLLIN   = 0x0001;
constexpr int16_t LINUX_POLLOUT  = 0x0004;
constexpr int16_t LINUX_POLLERR  = 0x0008;
constexpr int16_t LINUX_POLLHUP  = 0x0010;
constexpr int16_t LINUX_POLLNVAL = 0x0020;

constexpr int64_t LINUX_EPOLL_CLOEXEC = static_cast<int64_t>(LINUX_O_CLOEXEC);
constexpr int64_t LINUX_EPOLL_CTL_ADD = 1;
constexpr int64_t LINUX_EPOLL_CTL_DEL = 2;
constexpr int64_t LINUX_EPOLL_CTL_MOD = 3;

constexpr uint32_t LINUX_EPOLLIN      = 0x00000001u;
constexpr uint32_t LINUX_EPOLLOUT     = 0x00000004u;
constexpr uint32_t LINUX_EPOLLERR     = 0x00000008u;
constexpr uint32_t LINUX_EPOLLHUP     = 0x00000010u;
constexpr uint32_t LINUX_EPOLLRDHUP   = 0x00002000u;
constexpr uint32_t LINUX_EPOLLEXCLUSIVE = (1u << 28);
constexpr uint32_t LINUX_EPOLLWAKEUP  = (1u << 29);
constexpr uint32_t LINUX_EPOLLONESHOT = (1u << 30);
constexpr uint32_t LINUX_EPOLLET      = (1u << 31);

constexpr uint32_t LINUX_EPOLL_SUPPORTED_EVENTS = (LINUX_EPOLLIN | LINUX_EPOLLOUT | LINUX_EPOLLERR | LINUX_EPOLLHUP | LINUX_EPOLLRDHUP | LINUX_EPOLLEXCLUSIVE
                                                   | LINUX_EPOLLWAKEUP | LINUX_EPOLLONESHOT | LINUX_EPOLLET);

constexpr int64_t LINUX_AT_FDCWD            = -100;
constexpr int64_t LINUX_AT_SYMLINK_NOFOLLOW = 0x100;
constexpr int64_t LINUX_AT_NO_AUTOMOUNT     = 0x800;
constexpr int64_t LINUX_AT_EMPTY_PATH       = 0x1000;

constexpr uint8_t LINUX_DT_UNKNOWN = 0;
constexpr uint8_t LINUX_DT_CHR     = 2;
constexpr uint8_t LINUX_DT_DIR     = 4;
constexpr uint8_t LINUX_DT_REG     = 8;
constexpr uint8_t LINUX_DT_LNK     = 10;

constexpr uint64_t LINUX_ARCH_SET_FS = 0x1002;
constexpr uint64_t LINUX_ARCH_GET_FS = 0x1003;

constexpr int64_t LINUX_SIG_BLOCK   = 0;
constexpr int64_t LINUX_SIG_UNBLOCK = 1;
constexpr int64_t LINUX_SIG_SETMASK = 2;

constexpr int64_t LINUX_SIG_DFL = 0;
constexpr int64_t LINUX_SIG_IGN = 1;

constexpr uint64_t LINUX_RT_SIGSET_SIZE    = sizeof(uint64_t);
constexpr uint64_t LINUX_RT_SIGACTION_SIZE = sizeof(uint64_t) * 4;
constexpr int64_t  LINUX_SIGNAL_MIN        = 1;
constexpr int64_t  LINUX_SIGNAL_MAX        = static_cast<int64_t>(MAX_POSIX_SIGNALS_PER_PROCESS);

constexpr uint64_t LINUX_SIGKILL_MASK            = (1ULL << (9 - 1));
constexpr uint64_t LINUX_SIGSTOP_MASK            = (1ULL << (19 - 1));
constexpr uint64_t LINUX_UNBLOCKABLE_SIGNAL_MASK = (LINUX_SIGKILL_MASK | LINUX_SIGSTOP_MASK);

constexpr int64_t LINUX_SIGNAL_SIGKILL = 9;
constexpr int64_t LINUX_SIGNAL_SIGSTOP = 19;

constexpr int64_t LINUX_ITIMER_REAL    = 0;
constexpr int64_t LINUX_ITIMER_VIRTUAL = 1;
constexpr int64_t LINUX_ITIMER_PROF    = 2;

constexpr uint32_t LINUX_UMASK_PERMISSION_BITS = 0777;

constexpr int64_t LINUX_CLOCK_REALTIME         = 0;
constexpr int64_t LINUX_CLOCK_MONOTONIC        = 1;
constexpr int64_t LINUX_CLOCK_PROCESS_CPUTIME  = 2;
constexpr int64_t LINUX_CLOCK_THREAD_CPUTIME   = 3;
constexpr int64_t LINUX_CLOCK_MONOTONIC_RAW    = 4;
constexpr int64_t LINUX_CLOCK_REALTIME_COARSE  = 5;
constexpr int64_t LINUX_CLOCK_MONOTONIC_COARSE = 6;
constexpr int64_t LINUX_CLOCK_BOOTTIME         = 7;
constexpr int64_t LINUX_CLOCK_REALTIME_ALARM   = 8;
constexpr int64_t LINUX_CLOCK_BOOTTIME_ALARM   = 9;

constexpr int64_t LINUX_RLIMIT_CPU        = 0;
constexpr int64_t LINUX_RLIMIT_FSIZE      = 1;
constexpr int64_t LINUX_RLIMIT_DATA       = 2;
constexpr int64_t LINUX_RLIMIT_STACK      = 3;
constexpr int64_t LINUX_RLIMIT_CORE       = 4;
constexpr int64_t LINUX_RLIMIT_RSS        = 5;
constexpr int64_t LINUX_RLIMIT_NPROC      = 6;
constexpr int64_t LINUX_RLIMIT_NOFILE     = 7;
constexpr int64_t LINUX_RLIMIT_MEMLOCK    = 8;
constexpr int64_t LINUX_RLIMIT_AS         = 9;
constexpr int64_t LINUX_RLIMIT_LOCKS      = 10;
constexpr int64_t LINUX_RLIMIT_SIGPENDING = 11;
constexpr int64_t LINUX_RLIMIT_MSGQUEUE   = 12;
constexpr int64_t LINUX_RLIMIT_NICE       = 13;
constexpr int64_t LINUX_RLIMIT_RTPRIO     = 14;
constexpr int64_t LINUX_RLIMIT_RTTIME     = 15;

constexpr uint64_t LINUX_RLIM_INFINITY            = ~0ULL;
constexpr uint64_t LINUX_RLIMIT_STACK_DEFAULT     = (8ULL * 1024ULL * 1024ULL);
constexpr uint64_t LINUX_RLIMIT_MEMLOCK_DEFAULT   = (64ULL * 1024ULL);
constexpr uint64_t LINUX_RLIMIT_MSGQUEUE_DEFAULT  = 819200ULL;
constexpr uint64_t LINUX_RLIMIT_NICE_DEFAULT      = 0;
constexpr uint64_t LINUX_RLIMIT_RTPRIO_DEFAULT    = 0;
constexpr uint64_t LINUX_RLIMIT_RTTIME_DEFAULT    = LINUX_RLIM_INFINITY;

constexpr uint64_t LINUX_TIMER_NANOSECONDS_PER_TICK = 10000000;
constexpr uint64_t LINUX_TIMER_MICROSECONDS_PER_TICK = (LINUX_TIMER_NANOSECONDS_PER_TICK / 1000);
constexpr uint64_t LINUX_TIMER_TICKS_PER_SECOND = (1000000000ULL / LINUX_TIMER_NANOSECONDS_PER_TICK);

bool IsSupportedLinuxClockId(int64_t ClockId)
{
    switch (ClockId)
    {
        case LINUX_CLOCK_REALTIME:
        case LINUX_CLOCK_MONOTONIC:
        case LINUX_CLOCK_PROCESS_CPUTIME:
        case LINUX_CLOCK_THREAD_CPUTIME:
        case LINUX_CLOCK_MONOTONIC_RAW:
        case LINUX_CLOCK_REALTIME_COARSE:
        case LINUX_CLOCK_MONOTONIC_COARSE:
        case LINUX_CLOCK_BOOTTIME:
        case LINUX_CLOCK_REALTIME_ALARM:
        case LINUX_CLOCK_BOOTTIME_ALARM:
            return true;
        default:
            break;
    }

    return false;
}

bool IsSupportedLinuxRlimitResource(int64_t Resource)
{
    return (Resource >= LINUX_RLIMIT_CPU) && (Resource <= LINUX_RLIMIT_RTTIME);
}

uint64_t GetDefaultLinuxRlimitCurrent(const Process* CurrentProcess, int64_t Resource)
{
    switch (Resource)
    {
        case LINUX_RLIMIT_STACK:
            if (CurrentProcess != nullptr && CurrentProcess->AddressSpace != nullptr && CurrentProcess->AddressSpace->GetStackSize() != 0)
            {
                return CurrentProcess->AddressSpace->GetStackSize();
            }
            return LINUX_RLIMIT_STACK_DEFAULT;
        case LINUX_RLIMIT_NOFILE:
            return MAX_OPEN_FILES_PER_PROCESS;
        case LINUX_RLIMIT_NPROC:
            return MAX_PROCESSES;
        case LINUX_RLIMIT_MEMLOCK:
            return LINUX_RLIMIT_MEMLOCK_DEFAULT;
        case LINUX_RLIMIT_SIGPENDING:
            return MAX_POSIX_SIGNALS_PER_PROCESS;
        case LINUX_RLIMIT_MSGQUEUE:
            return LINUX_RLIMIT_MSGQUEUE_DEFAULT;
        case LINUX_RLIMIT_NICE:
            return LINUX_RLIMIT_NICE_DEFAULT;
        case LINUX_RLIMIT_RTPRIO:
            return LINUX_RLIMIT_RTPRIO_DEFAULT;
        case LINUX_RLIMIT_RTTIME:
            return LINUX_RLIMIT_RTTIME_DEFAULT;
        default:
            return LINUX_RLIM_INFINITY;
    }
}

uint64_t GetDefaultLinuxRlimitMaximum(const Process* CurrentProcess, int64_t Resource)
{
    switch (Resource)
    {
        case LINUX_RLIMIT_STACK:
            return LINUX_RLIM_INFINITY;
        case LINUX_RLIMIT_NOFILE:
            return MAX_OPEN_FILES_PER_PROCESS;
        case LINUX_RLIMIT_NPROC:
            return MAX_PROCESSES;
        case LINUX_RLIMIT_MEMLOCK:
            return LINUX_RLIMIT_MEMLOCK_DEFAULT;
        case LINUX_RLIMIT_SIGPENDING:
            return MAX_POSIX_SIGNALS_PER_PROCESS;
        case LINUX_RLIMIT_MSGQUEUE:
            return LINUX_RLIMIT_MSGQUEUE_DEFAULT;
        case LINUX_RLIMIT_NICE:
            return LINUX_RLIMIT_NICE_DEFAULT;
        case LINUX_RLIMIT_RTPRIO:
            return LINUX_RLIMIT_RTPRIO_DEFAULT;
        case LINUX_RLIMIT_RTTIME:
            return LINUX_RLIMIT_RTTIME_DEFAULT;
        default:
            break;
    }

    return GetDefaultLinuxRlimitCurrent(CurrentProcess, Resource);
}

void EnsureProcessLinuxResourceLimitsInitialized(Process* CurrentProcess)
{
    if (CurrentProcess == nullptr || CurrentProcess->ResourceLimitsInitialized)
    {
        return;
    }

    for (int64_t Resource = LINUX_RLIMIT_CPU; Resource <= LINUX_RLIMIT_RTTIME; ++Resource)
    {
        uint64_t DefaultCurrent = GetDefaultLinuxRlimitCurrent(CurrentProcess, Resource);
        uint64_t DefaultMaximum = GetDefaultLinuxRlimitMaximum(CurrentProcess, Resource);

        if (DefaultCurrent > DefaultMaximum)
        {
            DefaultCurrent = DefaultMaximum;
        }

        CurrentProcess->ResourceLimitCurrent[Resource] = DefaultCurrent;
        CurrentProcess->ResourceLimitMaximum[Resource] = DefaultMaximum;
    }

    CurrentProcess->ResourceLimitsInitialized = true;
}

bool IsCanonicalX86_64Address(uint64_t Address)
{
    constexpr uint64_t LOWER_CANONICAL_MAX = 0x00007FFFFFFFFFFFULL;
    constexpr uint64_t UPPER_CANONICAL_MIN = 0xFFFF800000000000ULL;
    return (Address <= LOWER_CANONICAL_MAX) || (Address >= UPPER_CANONICAL_MIN);
}

struct __attribute__((packed)) LinuxDirent64Header
{
    uint64_t Inode;
    uint64_t Offset;
    uint16_t RecordLength;
    uint8_t  Type;
};

FileFlags DecodeAccessFlags(uint64_t Flags)
{
    uint64_t AccessMode = Flags & LINUX_O_ACCMODE;

    if (AccessMode == 1)
    {
        return WRITE;
    }

    if (AccessMode == 2)
    {
        return READ_WRITE;
    }

    return READ;
}

bool CopyUserCString(LogicLayer* Logic, const char* UserString, char* KernelBuffer, uint64_t KernelBufferSize)
{
    if (Logic == nullptr || UserString == nullptr || KernelBuffer == nullptr || KernelBufferSize == 0)
    {
        return false;
    }

    constexpr uint64_t USER_STRING_COPY_CHUNK_SIZE = 64;

    uint64_t Index = 0;
    while (Index < KernelBufferSize)
    {
        uint64_t Remaining = KernelBufferSize - Index;
        uint64_t ChunkSize = (Remaining < USER_STRING_COPY_CHUNK_SIZE) ? Remaining : USER_STRING_COPY_CHUNK_SIZE;

        uint64_t UserAddress       = reinterpret_cast<uint64_t>(UserString) + Index;
        uint64_t OffsetWithinPage  = (UserAddress & (PAGE_SIZE - 1));
        uint64_t BytesUntilPageEnd = PAGE_SIZE - OffsetWithinPage;
        if (ChunkSize > BytesUntilPageEnd)
        {
            ChunkSize = BytesUntilPageEnd;
        }

        if (!Logic->CopyFromUserToKernel(reinterpret_cast<const void*>(UserAddress), KernelBuffer + Index, ChunkSize))
        {
            return false;
        }

        for (uint64_t ChunkIndex = 0; ChunkIndex < ChunkSize; ++ChunkIndex)
        {
            if (KernelBuffer[Index + ChunkIndex] == '\0')
            {
                return true;
            }
        }

        Index += ChunkSize;
    }

    KernelBuffer[KernelBufferSize - 1] = '\0';
    return false;
}

uint32_t BuildLinuxModeFromNode(const INode* Node)
{
    if (Node == nullptr)
    {
        return 0;
    }

    switch (Node->NodeType)
    {
        case INODE_DIR:
            return (LINUX_S_IFDIR | LINUX_DEFAULT_DIR_PERMISSIONS);
        case INODE_SYMLINK:
            return (LINUX_S_IFLNK | LINUX_DEFAULT_LINK_PERMISSIONS);
        case INODE_DEV:
            return (LINUX_S_IFCHR | LINUX_DEFAULT_CHAR_PERMISSIONS);
        case INODE_FILE:
        default:
            return (LINUX_S_IFREG | LINUX_DEFAULT_FILE_PERMISSIONS);
    }
}

uint64_t CStrLength(const char* String)
{
    if (String == nullptr)
    {
        return 0;
    }

    uint64_t Length = 0;
    while (String[Length] != '\0')
    {
        ++Length;
    }

    return Length;
}

bool CStrEquals(const char* Left, const char* Right)
{
    if (Left == nullptr || Right == nullptr)
    {
        return false;
    }

    uint64_t Index = 0;
    while (Left[Index] != '\0' && Right[Index] != '\0')
    {
        if (Left[Index] != Right[Index])
        {
            return false;
        }

        ++Index;
    }

    return Left[Index] == Right[Index];
}

bool IsSupportedMountFileSystemType(const char* FileSystemType)
{
    return CStrEquals(FileSystemType, "ext2");
}

uint64_t AlignUpValue(uint64_t Value, uint64_t Alignment)
{
    if (Alignment == 0)
    {
        return Value;
    }

    uint64_t Remainder = Value % Alignment;
    if (Remainder == 0)
    {
        return Value;
    }

    return Value + (Alignment - Remainder);
}

uint8_t BuildLinuxDirentTypeFromNode(const INode* Node)
{
    if (Node == nullptr)
    {
        return LINUX_DT_UNKNOWN;
    }

    switch (Node->NodeType)
    {
        case INODE_DIR:
            return LINUX_DT_DIR;
        case INODE_SYMLINK:
            return LINUX_DT_LNK;
        case INODE_DEV:
            return LINUX_DT_CHR;
        case INODE_FILE:
        default:
            return LINUX_DT_REG;
    }
}

void PopulateLinuxStatFromNode(const INode* Node, LinuxStat* KernelStat)
{
    if (Node == nullptr || KernelStat == nullptr)
    {
        return;
    }

    *KernelStat                       = {};
    KernelStat->Device                = 1;
    KernelStat->Inode                 = reinterpret_cast<uint64_t>(Node);
    KernelStat->HardLinkCount         = (Node->NodeType == INODE_DIR) ? 2 : 1;
    KernelStat->Mode                  = BuildLinuxModeFromNode(Node);
    KernelStat->UserId                = 0;
    KernelStat->GroupId               = 0;
    KernelStat->Padding0              = 0;
    KernelStat->SpecialDevice         = (Node->NodeType == INODE_DEV) ? KernelStat->Inode : 0;
    KernelStat->Size                  = static_cast<int64_t>(Node->NodeSize);
    KernelStat->BlockSize             = static_cast<int64_t>(PAGE_SIZE);
    KernelStat->Blocks                = static_cast<int64_t>((Node->NodeSize + 511) / 512);
    KernelStat->AccessTimeSeconds     = 0;
    KernelStat->AccessTimeNanoseconds = 0;
    KernelStat->ModifyTimeSeconds     = 0;
    KernelStat->ModifyTimeNanoseconds = 0;
    KernelStat->ChangeTimeSeconds     = 0;
    KernelStat->ChangeTimeNanoseconds = 0;
}

int64_t AllocateProcessFileDescriptor(Process* CurrentProcess, Dentry* NodeDentry, uint64_t Flags)
{
    if (CurrentProcess == nullptr || NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if ((Flags & LINUX_O_ACCMODE) == LINUX_O_ACCMODE)
    {
        return LINUX_ERR_EINVAL;
    }

    for (size_t FileDescriptor = 0; FileDescriptor < MAX_OPEN_FILES_PER_PROCESS; ++FileDescriptor)
    {
        if (CurrentProcess->FileTable[FileDescriptor] == nullptr)
        {
            File* NewFile = new File;
            if (NewFile == nullptr)
            {
                return LINUX_ERR_ENOMEM;
            }

            NewFile->FileDescriptor  = FileDescriptor;
            NewFile->Node            = NodeDentry->inode;
            NewFile->CurrentOffset   = 0;
            NewFile->AccessFlags     = DecodeAccessFlags(Flags);
            NewFile->OpenFlags       = Flags;
            NewFile->DescriptorFlags = ((Flags & LINUX_O_CLOEXEC) != 0) ? LINUX_FD_CLOEXEC : 0;
            NewFile->DirectoryEntry  = NodeDentry;

            CurrentProcess->FileTable[FileDescriptor] = NewFile;
            return static_cast<int64_t>(FileDescriptor);
        }
    }

    return LINUX_ERR_EMFILE;
}

bool HasPathSeparator(const char* Path)
{
    if (Path == nullptr)
    {
        return false;
    }

    for (uint64_t Index = 0; Path[Index] != '\0'; ++Index)
    {
        if (Path[Index] == '/')
        {
            return true;
        }
    }

    return false;
}

Dentry* ResolveSimpleRelativeChildDentry(Dentry* BaseDirectory, const char* RelativePath, bool FollowFinalSymlink)
{
    if (BaseDirectory == nullptr || RelativePath == nullptr || BaseDirectory->inode == nullptr)
    {
        return nullptr;
    }

    if (BaseDirectory->inode->NodeType != INODE_DIR)
    {
        return nullptr;
    }

    if (RelativePath[0] == '\0')
    {
        return nullptr;
    }

    if (RelativePath[0] == '.' && RelativePath[1] == '\0')
    {
        return BaseDirectory;
    }

    if (RelativePath[0] == '.' && RelativePath[1] == '.' && RelativePath[2] == '\0')
    {
        return (BaseDirectory->parent != nullptr) ? BaseDirectory->parent : BaseDirectory;
    }

    if (HasPathSeparator(RelativePath))
    {
        return nullptr;
    }

    uint64_t NameLength = CStrLength(RelativePath);
    if (NameLength == 0)
    {
        return nullptr;
    }

    for (uint64_t ChildIndex = 0; ChildIndex < BaseDirectory->child_count; ++ChildIndex)
    {
        Dentry* Child = BaseDirectory->children[ChildIndex];
        if (Child == nullptr || Child->inode == nullptr || Child->name == nullptr)
        {
            continue;
        }

        bool NamesMatch = true;
        for (uint64_t NameIndex = 0; NameIndex < NameLength; ++NameIndex)
        {
            if (Child->name[NameIndex] != RelativePath[NameIndex])
            {
                NamesMatch = false;
                break;
            }
        }

        if (!NamesMatch || Child->name[NameLength] != '\0')
        {
            continue;
        }

        if (!FollowFinalSymlink && Child->inode->NodeType == INODE_SYMLINK)
        {
            return Child;
        }

        if (Child->inode->NodeType == INODE_SYMLINK)
        {
            return nullptr;
        }

        return Child;
    }

    return nullptr;
}

void FreeKernelStringVector(LogicLayer* Logic, char** KernelVector, uint64_t Count)
{
    if (Logic == nullptr || KernelVector == nullptr)
    {
        return;
    }

    for (uint64_t Index = 0; Index < Count; ++Index)
    {
        if (KernelVector[Index] != nullptr)
        {
            Logic->kfree(KernelVector[Index]);
        }
    }

    Logic->kfree(KernelVector);
}

bool CopyUserStringVector(LogicLayer* Logic, const char* const* UserVector, char*** KernelVectorOut, uint64_t* CountOut)
{
    if (Logic == nullptr || KernelVectorOut == nullptr || CountOut == nullptr)
    {
        return false;
    }

    *KernelVectorOut = nullptr;
    *CountOut        = 0;

    if (UserVector == nullptr)
    {
        return true;
    }

    char** KernelVector = reinterpret_cast<char**>(Logic->kmalloc(sizeof(char*) * (SYSCALL_EXEC_MAX_VECTOR + 1)));
    if (KernelVector == nullptr)
    {
        return false;
    }

    for (uint64_t Index = 0; Index < (SYSCALL_EXEC_MAX_VECTOR + 1); ++Index)
    {
        KernelVector[Index] = nullptr;
    }

    char TempBuffer[SYSCALL_PATH_MAX];

    for (uint64_t Index = 0; Index < SYSCALL_EXEC_MAX_VECTOR; ++Index)
    {
        const char* UserEntry        = nullptr;
        const void* UserEntryAddress = reinterpret_cast<const void*>(reinterpret_cast<uint64_t>(UserVector) + (Index * sizeof(const char*)));
        if (!Logic->CopyFromUserToKernel(UserEntryAddress, &UserEntry, sizeof(UserEntry)))
        {
#ifdef DEBUG_BUILD
            Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
            if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr)
            {
                TTY* Terminal = ActiveDispatcher->GetResourceLayer()->GetTTY();
                if (Terminal != nullptr)
                {
                    Terminal->Serialprintf("execvec_dbg: read_ptr_fail vec=%p idx=%lu slot=%p\n", (void*) UserVector, Index, UserEntryAddress);
                }
            }
#endif
            FreeKernelStringVector(Logic, KernelVector, Index);
            return false;
        }

        if (UserEntry == nullptr)
        {
            *KernelVectorOut = KernelVector;
            *CountOut        = Index;
            return true;
        }

        if (!CopyUserCString(Logic, UserEntry, TempBuffer, sizeof(TempBuffer)))
        {
#ifdef DEBUG_BUILD
            Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
            if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr)
            {
                TTY* Terminal = ActiveDispatcher->GetResourceLayer()->GetTTY();
                if (Terminal != nullptr)
                {
                    Terminal->Serialprintf("execvec_dbg: read_cstr_fail vec=%p idx=%lu entry=%p\n", (void*) UserVector, Index, (void*) UserEntry);
                }
            }
#endif
            FreeKernelStringVector(Logic, KernelVector, Index);
            return false;
        }

        uint64_t EntryLength = CStrLength(TempBuffer) + 1;
        KernelVector[Index]  = reinterpret_cast<char*>(Logic->kmalloc(EntryLength));
        if (KernelVector[Index] == nullptr)
        {
            FreeKernelStringVector(Logic, KernelVector, Index);
            return false;
        }

        for (uint64_t CharIndex = 0; CharIndex < EntryLength; ++CharIndex)
        {
            KernelVector[Index][CharIndex] = TempBuffer[CharIndex];
        }
    }

#ifdef DEBUG_BUILD
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr)
    {
        TTY* Terminal = ActiveDispatcher->GetResourceLayer()->GetTTY();
        if (Terminal != nullptr)
        {
            Terminal->Serialprintf("execvec_dbg: vector_too_large vec=%p max=%lu\n", (void*) UserVector, static_cast<uint64_t>(SYSCALL_EXEC_MAX_VECTOR));
        }
    }
#endif

    FreeKernelStringVector(Logic, KernelVector, SYSCALL_EXEC_MAX_VECTOR);
    return false;
}

bool BuildAbsolutePathFromDentry(const Dentry* Node, char* Buffer, uint64_t BufferSize)
{
    if (Node == nullptr || Buffer == nullptr || BufferSize == 0)
    {
        return false;
    }

    const char* SegmentStack[SYSCALL_MAX_PATH_SEGMENTS] = {};
    uint64_t    SegmentCount                            = 0;

    const Dentry* Current = Node;
    while (Current != nullptr && Current->parent != nullptr)
    {
        if (SegmentCount >= SYSCALL_MAX_PATH_SEGMENTS)
        {
            return false;
        }

        if (Current->name == nullptr)
        {
            return false;
        }

        SegmentStack[SegmentCount++] = Current->name;
        Current                      = Current->parent;
    }

    uint64_t Cursor  = 0;
    Buffer[Cursor++] = '/';

    if (SegmentCount == 0)
    {
        if (Cursor >= BufferSize)
        {
            return false;
        }

        Buffer[Cursor] = '\0';
        return true;
    }

    for (uint64_t SegmentIndex = SegmentCount; SegmentIndex > 0; --SegmentIndex)
    {
        const char* Segment       = SegmentStack[SegmentIndex - 1];
        uint64_t    SegmentLength = CStrLength(Segment);
        if (SegmentLength == 0)
        {
            continue;
        }

        if ((Cursor + SegmentLength) >= BufferSize)
        {
            return false;
        }

        memcpy(Buffer + Cursor, Segment, static_cast<size_t>(SegmentLength));
        Cursor += SegmentLength;

        if (SegmentIndex != 1)
        {
            if (Cursor >= BufferSize)
            {
                return false;
            }

            Buffer[Cursor++] = '/';
        }
    }

    if (Cursor >= BufferSize)
    {
        return false;
    }

    Buffer[Cursor] = '\0';
    return true;
}

void ReleaseVforkParentIfNeeded(LogicLayer* Logic, Process* ChildProcess)
{
    if (Logic == nullptr || ChildProcess == nullptr || !ChildProcess->IsVforkChild)
    {
        return;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return;
    }

    uint8_t ParentId = ChildProcess->VforkParentId;

    ChildProcess->IsVforkChild  = false;
    ChildProcess->VforkParentId = PROCESS_ID_INVALID;

    Process* ParentProcess = PM->GetProcessById(ParentId);
    if (ParentProcess == nullptr || ParentProcess->Status == PROCESS_TERMINATED)
    {
        return;
    }

    bool ShouldUnblockParent            = ParentProcess->WaitingForVforkChild && ParentProcess->VforkChildId == ChildProcess->Id;
    ParentProcess->WaitingForVforkChild = false;
    ParentProcess->VforkChildId         = PROCESS_ID_INVALID;

    if (ShouldUnblockParent)
    {
        Logic->UnblockProcess(ParentProcess->Id);
    }
}

void SyncVforkParentFileSystemLocation(ProcessManager* PM, Process* CurrentProcess)
{
    if (PM == nullptr || CurrentProcess == nullptr || !CurrentProcess->IsVforkChild)
    {
        return;
    }

    Process* ParentProcess = PM->GetProcessById(CurrentProcess->VforkParentId);
    if (ParentProcess == nullptr || ParentProcess->Status == PROCESS_TERMINATED)
    {
        return;
    }

    ParentProcess->CurrentFileSystemLocation = CurrentProcess->CurrentFileSystemLocation;
}

uint64_t AlignDownToPageBoundary(uint64_t Address)
{
    return Address & PHYS_PAGE_ADDR_MASK;
}

uint64_t AlignUpToPageBoundary(uint64_t Value)
{
    if (Value == 0)
    {
        return 0;
    }

    if (Value > (UINT64_MAX - (PAGE_SIZE - 1)))
    {
        return 0;
    }

    return (Value + PAGE_SIZE - 1) & PHYS_PAGE_ADDR_MASK;
}

bool RangesOverlap(uint64_t StartA, uint64_t LengthA, uint64_t StartB, uint64_t LengthB)
{
    if (LengthA == 0 || LengthB == 0)
    {
        return false;
    }

    uint64_t EndA = StartA + LengthA;
    uint64_t EndB = StartB + LengthB;
    if (EndA < StartA || EndB < StartB)
    {
        return true;
    }

    return (StartA < EndB) && (StartB < EndA);
}

bool MappingOverlapsProcessLayout(const Process* CurrentProcess, uint64_t MappingStart, uint64_t MappingLength)
{
    if (CurrentProcess == nullptr || CurrentProcess->AddressSpace == nullptr)
    {
        return true;
    }

    const VirtualAddressSpace* AddressSpace = CurrentProcess->AddressSpace;

    if (CurrentProcess->FileType == FILE_TYPE_ELF)
    {
        const VirtualAddressSpaceELF* ELFAddressSpace = static_cast<const VirtualAddressSpaceELF*>(AddressSpace);
        const ELFMemoryRegion*        Regions         = ELFAddressSpace->GetMemoryRegions();
        size_t                        RegionCount     = ELFAddressSpace->GetMemoryRegionCount();

        for (size_t RegionIndex = 0; RegionIndex < RegionCount; ++RegionIndex)
        {
            if (RangesOverlap(MappingStart, MappingLength, Regions[RegionIndex].VirtualAddress, Regions[RegionIndex].Size))
            {
                return true;
            }
        }
    }

    if (RangesOverlap(MappingStart, MappingLength, AddressSpace->GetCodeVirtualAddressStart(), AddressSpace->GetCodeSize())
        || RangesOverlap(MappingStart, MappingLength, AddressSpace->GetHeapVirtualAddressStart(), AddressSpace->GetHeapSize())
        || RangesOverlap(MappingStart, MappingLength, AddressSpace->GetStackVirtualAddressStart(), AddressSpace->GetStackSize()))
    {
        return true;
    }

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        const ProcessMemoryMapping& ExistingMapping = CurrentProcess->MemoryMappings[MappingIndex];
        if (!ExistingMapping.InUse)
        {
            continue;
        }

        if (RangesOverlap(MappingStart, MappingLength, ExistingMapping.VirtualAddressStart, ExistingMapping.Length))
        {
            return true;
        }
    }

    return false;
}

bool MappingOverlapsReservedProcessLayout(const Process* CurrentProcess, uint64_t MappingStart, uint64_t MappingLength)
{
    if (CurrentProcess == nullptr || CurrentProcess->AddressSpace == nullptr)
    {
        return true;
    }

    const VirtualAddressSpace* AddressSpace = CurrentProcess->AddressSpace;

    if (CurrentProcess->FileType == FILE_TYPE_ELF)
    {
        const VirtualAddressSpaceELF* ELFAddressSpace = static_cast<const VirtualAddressSpaceELF*>(AddressSpace);
        const ELFMemoryRegion*        Regions         = ELFAddressSpace->GetMemoryRegions();
        size_t                        RegionCount     = ELFAddressSpace->GetMemoryRegionCount();

        for (size_t RegionIndex = 0; RegionIndex < RegionCount; ++RegionIndex)
        {
            if (RangesOverlap(MappingStart, MappingLength, Regions[RegionIndex].VirtualAddress, Regions[RegionIndex].Size))
            {
                return true;
            }
        }
    }

    return RangesOverlap(MappingStart, MappingLength, AddressSpace->GetCodeVirtualAddressStart(), AddressSpace->GetCodeSize())
        || RangesOverlap(MappingStart, MappingLength, AddressSpace->GetHeapVirtualAddressStart(), AddressSpace->GetHeapSize())
        || RangesOverlap(MappingStart, MappingLength, AddressSpace->GetStackVirtualAddressStart(), AddressSpace->GetStackSize());
}

int64_t FindAvailableMappingSlot(const Process* CurrentProcess)
{
    if (CurrentProcess == nullptr)
    {
        return -1;
    }

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        if (!CurrentProcess->MemoryMappings[MappingIndex].InUse)
        {
            return static_cast<int64_t>(MappingIndex);
        }
    }

    return -1;
}

bool RegisterProcessMapping(Process* CurrentProcess, uint64_t MappingStart, uint64_t MappingLength, uint64_t PhysicalStart, bool IsAnonymous)
{
    int64_t MappingSlot = FindAvailableMappingSlot(CurrentProcess);
    if (MappingSlot < 0)
    {
        return false;
    }

    ProcessMemoryMapping& Mapping = CurrentProcess->MemoryMappings[static_cast<size_t>(MappingSlot)];
    Mapping.InUse                 = true;
    Mapping.VirtualAddressStart   = MappingStart;
    Mapping.Length                = MappingLength;
    Mapping.PhysicalAddressStart  = PhysicalStart;
    Mapping.IsAnonymous           = IsAnonymous;
    return true;
}

bool IsVirtualAddressMapped(uint64_t PageMapL4TableAddr, uint64_t Address)
{
    if (PageMapL4TableAddr == 0)
    {
        return false;
    }

    VirtualAddress Vaddr;
    Vaddr.value = Address;

    PageTableEntry* PML4      = reinterpret_cast<PageTableEntry*>(PageMapL4TableAddr);
    PageTableEntry  PML4Entry = PML4[Vaddr.fields.pml4_index];
    if (!PML4Entry.fields.present)
    {
        return false;
    }

    PageTableEntry* PDPT = reinterpret_cast<PageTableEntry*>(PML4Entry.value & PHYS_PAGE_ADDR_MASK);
    if (PDPT == nullptr)
    {
        return false;
    }

    PageTableEntry PDPTEntry = PDPT[Vaddr.fields.pdpt_index];
    if (!PDPTEntry.fields.present)
    {
        return false;
    }

    PageTableEntry* PD = reinterpret_cast<PageTableEntry*>(PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
    if (PD == nullptr)
    {
        return false;
    }

    PageTableEntry PDEntry = PD[Vaddr.fields.pd_index];
    if (!PDEntry.fields.present)
    {
        return false;
    }

    PageTableEntry* PT = reinterpret_cast<PageTableEntry*>(PDEntry.value & PHYS_PAGE_ADDR_MASK);
    if (PT == nullptr)
    {
        return false;
    }

    PageTableEntry PTEntry = PT[Vaddr.fields.pt_index];
    return PTEntry.fields.present;
}

bool RangeHasAnyMappedPage(const Process* CurrentProcess, uint64_t MappingStart, uint64_t MappingLength)
{
    if (CurrentProcess == nullptr || CurrentProcess->AddressSpace == nullptr || MappingLength == 0)
    {
        return true;
    }

    uint64_t PageMapL4TableAddr = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
    if (PageMapL4TableAddr == 0)
    {
        return true;
    }

    uint64_t RangeStart = AlignDownToPageBoundary(MappingStart);
    uint64_t RangeEnd   = AlignUpToPageBoundary(MappingStart + MappingLength);
    if (RangeEnd <= RangeStart)
    {
        return true;
    }

    for (uint64_t Address = RangeStart; Address < RangeEnd; Address += PAGE_SIZE)
    {
        if (IsVirtualAddressMapped(PageMapL4TableAddr, Address))
        {
            return true;
        }
    }

    return false;
}

uint64_t FindFreeMappingAddress(const Process* CurrentProcess, uint64_t StartHint, uint64_t MappingLength)
{
    bool     NoHint    = (StartHint == 0);
    uint64_t Candidate = AlignDownToPageBoundary(StartHint);
    if (Candidate == 0)
    {
        if (CurrentProcess != nullptr && CurrentProcess->AddressSpace != nullptr)
        {
            uint64_t SuggestedStart = 0;

            if (CurrentProcess->FileType == FILE_TYPE_ELF)
            {
                const VirtualAddressSpaceELF* ELFAddressSpace = static_cast<const VirtualAddressSpaceELF*>(CurrentProcess->AddressSpace);
                const ELFMemoryRegion*        Regions         = ELFAddressSpace->GetMemoryRegions();
                size_t                        RegionCount     = ELFAddressSpace->GetMemoryRegionCount();

                uint64_t HighestELFEnd = 0;
                for (size_t RegionIndex = 0; RegionIndex < RegionCount; ++RegionIndex)
                {
                    uint64_t RegionStart = AlignDownToPageBoundary(Regions[RegionIndex].VirtualAddress);
                    uint64_t RegionEnd   = AlignUpToPageBoundary(Regions[RegionIndex].VirtualAddress + Regions[RegionIndex].Size);

                    if (RegionEnd > RegionStart && RegionEnd > HighestELFEnd)
                    {
                        HighestELFEnd = RegionEnd;
                    }
                }

                if (HighestELFEnd != 0 && HighestELFEnd <= (UINT64_MAX - PAGE_SIZE))
                {
                    SuggestedStart = HighestELFEnd + PAGE_SIZE;
                }
            }

            if (SuggestedStart == 0)
            {
                uint64_t HeapEnd = AlignUpToPageBoundary(CurrentProcess->AddressSpace->GetHeapVirtualAddressStart() + CurrentProcess->AddressSpace->GetHeapSize());
                if (HeapEnd != 0 && HeapEnd <= (UINT64_MAX - PAGE_SIZE))
                {
                    SuggestedStart = HeapEnd + PAGE_SIZE;
                }
            }

            if (SuggestedStart != 0)
            {
                Candidate = SuggestedStart;
            }
        }

        if (Candidate == 0)
        {
            Candidate = MMAP_DEFAULT_BASE;
        }
    }

    if (NoHint && Candidate < MMAP_HIGH_BASE)
    {
        Candidate = MMAP_HIGH_BASE;
    }

    auto UpdateOverlapEnd = [&](uint64_t RegionStart, uint64_t RegionLength, bool* HasOverlap, uint64_t* OverlapEnd) -> void
    {
        if (RegionLength == 0)
        {
            return;
        }

        if (!RangesOverlap(Candidate, MappingLength, RegionStart, RegionLength))
        {
            return;
        }

        uint64_t RegionEnd = AlignUpToPageBoundary(RegionStart + RegionLength);
        if (RegionEnd == 0)
        {
            RegionEnd = RegionStart + RegionLength;
        }

        *HasOverlap = true;
        if (RegionEnd > *OverlapEnd)
        {
            *OverlapEnd = RegionEnd;
        }
    };

    for (uint64_t Attempt = 0; Attempt < (1024 * 1024); ++Attempt)
    {
        bool     HasOverlap = false;
        uint64_t OverlapEnd = 0;

        if (CurrentProcess != nullptr && CurrentProcess->AddressSpace != nullptr)
        {
            const VirtualAddressSpace* AddressSpace = CurrentProcess->AddressSpace;

            if (CurrentProcess->FileType == FILE_TYPE_ELF)
            {
                const VirtualAddressSpaceELF* ELFAddressSpace = static_cast<const VirtualAddressSpaceELF*>(AddressSpace);
                const ELFMemoryRegion*        Regions         = ELFAddressSpace->GetMemoryRegions();
                size_t                        RegionCount     = ELFAddressSpace->GetMemoryRegionCount();

                for (size_t RegionIndex = 0; RegionIndex < RegionCount; ++RegionIndex)
                {
                    UpdateOverlapEnd(Regions[RegionIndex].VirtualAddress, Regions[RegionIndex].Size, &HasOverlap, &OverlapEnd);
                }
            }

            UpdateOverlapEnd(AddressSpace->GetCodeVirtualAddressStart(), AddressSpace->GetCodeSize(), &HasOverlap, &OverlapEnd);
            UpdateOverlapEnd(AddressSpace->GetHeapVirtualAddressStart(), AddressSpace->GetHeapSize(), &HasOverlap, &OverlapEnd);
            UpdateOverlapEnd(AddressSpace->GetStackVirtualAddressStart(), AddressSpace->GetStackSize(), &HasOverlap, &OverlapEnd);

            for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
            {
                const ProcessMemoryMapping& ExistingMapping = CurrentProcess->MemoryMappings[MappingIndex];
                if (!ExistingMapping.InUse)
                {
                    continue;
                }

                UpdateOverlapEnd(ExistingMapping.VirtualAddressStart, ExistingMapping.Length, &HasOverlap, &OverlapEnd);
            }
        }

        if (!HasOverlap)
        {
            if (!RangeHasAnyMappedPage(CurrentProcess, Candidate, MappingLength))
            {
                return Candidate;
            }

            constexpr uint64_t MMAP_PROBE_STEP = (PAGE_SIZE * 512);
            if (Candidate > (UINT64_MAX - MMAP_PROBE_STEP))
            {
                break;
            }

            Candidate = AlignUpToPageBoundary(Candidate + MMAP_PROBE_STEP);
            if (Candidate == 0)
            {
                break;
            }
            continue;
        }

        if (OverlapEnd == 0)
        {
            break;
        }

        Candidate = AlignUpToPageBoundary(OverlapEnd);
        if (Candidate == 0)
        {
            break;
        }
    }

    return 0;
}
} // namespace

/**
 * Function: TranslationLayer::TranslationLayer
 * Description: Constructs the translation layer with no attached logic layer.
 * Parameters:
 *   None.
 * Returns:
 *   TranslationLayer - Constructed translation layer instance.
 */
TranslationLayer::TranslationLayer() : Logic(nullptr)
{
}

/**
 * Function: TranslationLayer::Initialize
 * Description: Attaches the translation layer to the logic layer.
 * Parameters:
 *   LogicLayer* Logic - Logic layer instance used by translation layer.
 * Returns:
 *   void - Does not return a value.
 */
void TranslationLayer::Initialize(LogicLayer* Logic)
{
    this->Logic = Logic;
}

/**
 * Function: TranslationLayer::GetLogicLayer
 * Description: Returns the attached logic layer instance.
 * Parameters:
 *   None.
 * Returns:
 *   LogicLayer* - Pointer to the attached logic layer.
 */
LogicLayer* TranslationLayer::GetLogicLayer() const
{
    return Logic;
}

// Posix system call handlers

int64_t TranslationLayer::HandleReadSystemCall(uint64_t FileDescriptor, void* Buffer, uint64_t Count)
{
    if (Logic == nullptr || (Buffer == nullptr && Count != 0))
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->AccessFlags == WRITE)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->Node->FileOps == nullptr || OpenFile->Node->FileOps->Read == nullptr)
    {
        return LINUX_ERR_ENOSYS;
    }

    if (Count == 0)
    {
        return 0;
    }

    uint8_t  KernelBuffer[SYSCALL_COPY_CHUNK_SIZE];
    uint64_t TotalCopied = 0;

    while (TotalCopied < Count)
    {
        uint64_t Remaining = Count - TotalCopied;
        uint64_t ChunkSize = (Remaining < SYSCALL_COPY_CHUNK_SIZE) ? Remaining : SYSCALL_COPY_CHUNK_SIZE;

        int64_t BytesRead = OpenFile->Node->FileOps->Read(OpenFile, KernelBuffer, ChunkSize);
        if (BytesRead < 0)
        {
            return (TotalCopied == 0) ? BytesRead : static_cast<int64_t>(TotalCopied);
        }

        if (BytesRead == 0)
        {
            break;
        }

        void* UserChunkDestination = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(Buffer) + TotalCopied);
        if (!Logic->CopyFromKernelToUser(KernelBuffer, UserChunkDestination, static_cast<uint64_t>(BytesRead)))
        {
            return (TotalCopied == 0) ? LINUX_ERR_EFAULT : static_cast<int64_t>(TotalCopied);
        }

        TotalCopied += static_cast<uint64_t>(BytesRead);

        if (static_cast<uint64_t>(BytesRead) < ChunkSize)
        {
            break;
        }
    }

    return static_cast<int64_t>(TotalCopied);
}

int64_t TranslationLayer::HandleWriteSystemCall(uint64_t FileDescriptor, const void* Buffer, uint64_t Count)
{
    if (Logic == nullptr || (Buffer == nullptr && Count != 0))
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->AccessFlags == READ)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->Node->FileOps == nullptr || OpenFile->Node->FileOps->Write == nullptr)
    {
        return LINUX_ERR_ENOSYS;
    }

    if (Count == 0)
    {
        return 0;
    }

    uint8_t  KernelBuffer[SYSCALL_COPY_CHUNK_SIZE];
    uint64_t TotalCopied = 0;

    while (TotalCopied < Count)
    {
        uint64_t Remaining = Count - TotalCopied;
        uint64_t ChunkSize = (Remaining < SYSCALL_COPY_CHUNK_SIZE) ? Remaining : SYSCALL_COPY_CHUNK_SIZE;

        const void* UserChunkSource = reinterpret_cast<const void*>(reinterpret_cast<uint64_t>(Buffer) + TotalCopied);
        if (!Logic->CopyFromUserToKernel(UserChunkSource, KernelBuffer, ChunkSize))
        {
            return (TotalCopied == 0) ? LINUX_ERR_EFAULT : static_cast<int64_t>(TotalCopied);
        }

        int64_t BytesWritten = OpenFile->Node->FileOps->Write(OpenFile, KernelBuffer, ChunkSize);
        if (BytesWritten < 0)
        {
            return (TotalCopied == 0) ? BytesWritten : static_cast<int64_t>(TotalCopied);
        }

        if (BytesWritten == 0)
        {
            break;
        }

        TotalCopied += static_cast<uint64_t>(BytesWritten);

        if (static_cast<uint64_t>(BytesWritten) < ChunkSize)
        {
            break;
        }
    }

    return static_cast<int64_t>(TotalCopied);
}

int64_t TranslationLayer::HandleWritevSystemCall(uint64_t FileDescriptor, const void* Iov, uint64_t IovCount)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (IovCount == 0 || IovCount > SYSCALL_IOV_MAX)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Iov == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->AccessFlags == READ)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->Node->FileOps == nullptr || OpenFile->Node->FileOps->Write == nullptr)
    {
        return LINUX_ERR_ENOSYS;
    }

    uint64_t TotalWritten = 0;

    for (uint64_t Index = 0; Index < IovCount; ++Index)
    {
        LinuxIOVec  KernelIOVec      = {};
        const void* UserIOVecAddress = reinterpret_cast<const void*>(reinterpret_cast<uint64_t>(Iov) + (Index * sizeof(LinuxIOVec)));
        if (!Logic->CopyFromUserToKernel(UserIOVecAddress, &KernelIOVec, sizeof(KernelIOVec)))
        {
            return (TotalWritten == 0) ? LINUX_ERR_EFAULT : static_cast<int64_t>(TotalWritten);
        }

        if (KernelIOVec.Length == 0)
        {
            continue;
        }

        if (KernelIOVec.Base == 0)
        {
            return (TotalWritten == 0) ? LINUX_ERR_EFAULT : static_cast<int64_t>(TotalWritten);
        }

        uint8_t  KernelBuffer[SYSCALL_COPY_CHUNK_SIZE];
        uint64_t SegmentWritten = 0;

        while (SegmentWritten < KernelIOVec.Length)
        {
            uint64_t Remaining = KernelIOVec.Length - SegmentWritten;
            uint64_t ChunkSize = (Remaining < SYSCALL_COPY_CHUNK_SIZE) ? Remaining : SYSCALL_COPY_CHUNK_SIZE;

            const void* UserChunkSource = reinterpret_cast<const void*>(KernelIOVec.Base + SegmentWritten);
            if (!Logic->CopyFromUserToKernel(UserChunkSource, KernelBuffer, ChunkSize))
            {
                return (TotalWritten == 0) ? LINUX_ERR_EFAULT : static_cast<int64_t>(TotalWritten);
            }

            int64_t BytesWritten = OpenFile->Node->FileOps->Write(OpenFile, KernelBuffer, ChunkSize);
            if (BytesWritten < 0)
            {
                return (TotalWritten == 0) ? BytesWritten : static_cast<int64_t>(TotalWritten);
            }

            if (BytesWritten == 0)
            {
                break;
            }

            SegmentWritten += static_cast<uint64_t>(BytesWritten);

            if (static_cast<uint64_t>(BytesWritten) < ChunkSize)
            {
                break;
            }
        }

        if (SegmentWritten == 0)
        {
            break;
        }

        if (TotalWritten > (UINT64_MAX - SegmentWritten))
        {
            return (TotalWritten == 0) ? LINUX_ERR_EINVAL : static_cast<int64_t>(TotalWritten);
        }

        TotalWritten += SegmentWritten;

        if (SegmentWritten < KernelIOVec.Length)
        {
            break;
        }
    }

    return static_cast<int64_t>(TotalWritten);
}

int64_t TranslationLayer::HandleLseekSystemCall(uint64_t FileDescriptor, int64_t Offset, int64_t Whence)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (Whence != LINUX_SEEK_SET && Whence != LINUX_SEEK_CUR && Whence != LINUX_SEEK_END)
    {
        return LINUX_ERR_EINVAL;
    }

    if (OpenFile->Node->FileOps != nullptr && OpenFile->Node->FileOps->Seek != nullptr)
    {
        int64_t SeekResult = OpenFile->Node->FileOps->Seek(OpenFile, Offset, static_cast<int32_t>(Whence));
        if (SeekResult != LINUX_ERR_ENOSYS)
        {
            return SeekResult;
        }
    }

    if (OpenFile->Node->NodeType == INODE_DEV)
    {
        return LINUX_ERR_ESPIPE;
    }

    int64_t BaseOffset = 0;
    switch (Whence)
    {
        case LINUX_SEEK_SET:
            BaseOffset = 0;
            break;
        case LINUX_SEEK_CUR:
            BaseOffset = static_cast<int64_t>(OpenFile->CurrentOffset);
            break;
        case LINUX_SEEK_END:
            BaseOffset = static_cast<int64_t>(OpenFile->Node->NodeSize);
            break;
        default:
            return LINUX_ERR_EINVAL;
    }

    if ((Offset > 0 && BaseOffset > (INT64_MAX - Offset)) || (Offset < 0 && BaseOffset < (INT64_MIN - Offset)))
    {
        return LINUX_ERR_EINVAL;
    }

    int64_t NewOffset = BaseOffset + Offset;
    if (NewOffset < 0)
    {
        return LINUX_ERR_EINVAL;
    }

    OpenFile->CurrentOffset = static_cast<uint64_t>(NewOffset);
    return NewOffset;
}

int64_t TranslationLayer::HandlePollSystemCall(void* PollFdArray, uint64_t PollFdCount, int64_t TimeoutMilliseconds)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (PollFdCount > MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EINVAL;
    }

    if (PollFdCount > 0 && PollFdArray == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (PollFdCount == 0)
    {
        if (TimeoutMilliseconds > 0)
        {
            uint64_t SleepTicks = static_cast<uint64_t>((TimeoutMilliseconds + 9) / 10);
            if (SleepTicks == 0)
            {
                SleepTicks = 1;
            }
            Logic->SleepProcess(CurrentProcess->Id, SleepTicks);
        }
        return 0;
    }

    auto EvaluatePollState = [&](bool* HasTTYReadableWait) -> int64_t
    {
        int64_t ReadyDescriptors = 0;
        bool    WantsTTYRead     = false;

        for (uint64_t Index = 0; Index < PollFdCount; ++Index)
        {
            LinuxPollFd KernelPollFd      = {};
            const void* UserPollFdAddress = reinterpret_cast<const void*>(reinterpret_cast<uint64_t>(PollFdArray) + (Index * sizeof(LinuxPollFd)));

            if (!Logic->CopyFromUserToKernel(UserPollFdAddress, &KernelPollFd, sizeof(KernelPollFd)))
            {
                return LINUX_ERR_EFAULT;
            }

            KernelPollFd.Revents = 0;

            if (KernelPollFd.FileDescriptor >= 0)
            {
                uint64_t FileDescriptor = static_cast<uint64_t>(KernelPollFd.FileDescriptor);
                if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
                {
                    KernelPollFd.Revents = LINUX_POLLNVAL;
                }
                else
                {
                    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
                    if (OpenFile == nullptr || OpenFile->Node == nullptr)
                    {
                        KernelPollFd.Revents = LINUX_POLLNVAL;
                    }
                    else
                    {
                        if (OpenFile->Node->FileOps != nullptr && OpenFile->Node->FileOps->Poll != nullptr)
                        {
                            uint32_t FileRevents = 0;
                            int64_t  PollResult  = OpenFile->Node->FileOps->Poll(OpenFile, static_cast<uint32_t>(KernelPollFd.Events), &FileRevents, Logic, CurrentProcess);
                            if (PollResult < 0)
                            {
                                return PollResult;
                            }

                            KernelPollFd.Revents = static_cast<int16_t>(FileRevents & 0xFFFFu);
                        }
                        else
                        {
                        if ((KernelPollFd.Events & LINUX_POLLIN) != 0)
                        {
                            bool IsTTYNode = (OpenFile->Node->NodeType == INODE_DEV && OpenFile->DirectoryEntry != nullptr && OpenFile->DirectoryEntry->name != nullptr
                                              && CStrEquals(OpenFile->DirectoryEntry->name, "tty"));
                            if (IsTTYNode)
                            {
                                WantsTTYRead  = true;
                                TTY* Terminal = reinterpret_cast<TTY*>(OpenFile->Node->NodeData);
                                if (Terminal != nullptr && Terminal->GetBufferedInputBytes() > 0)
                                {
                                    KernelPollFd.Revents |= LINUX_POLLIN;
                                }
                            }
                            else if (OpenFile->Node->NodeType != INODE_DIR)
                            {
                                KernelPollFd.Revents |= LINUX_POLLIN;
                            }
                        }

                        if ((KernelPollFd.Events & LINUX_POLLOUT) != 0 && OpenFile->AccessFlags != READ)
                        {
                            KernelPollFd.Revents |= LINUX_POLLOUT;
                        }
                        }
                    }
                }
            }

            if ((KernelPollFd.Revents & (LINUX_POLLERR | LINUX_POLLHUP | LINUX_POLLNVAL | LINUX_POLLIN | LINUX_POLLOUT)) != 0)
            {
                ++ReadyDescriptors;
            }

            void* UserPollFdWriteAddress = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(PollFdArray) + (Index * sizeof(LinuxPollFd)));
            if (!Logic->CopyFromKernelToUser(&KernelPollFd, UserPollFdWriteAddress, sizeof(KernelPollFd)))
            {
                return LINUX_ERR_EFAULT;
            }
        }

        if (HasTTYReadableWait != nullptr)
        {
            *HasTTYReadableWait = WantsTTYRead;
        }

        return ReadyDescriptors;
    };

    bool    HasTTYReadableWait = false;
    int64_t ReadyCount         = EvaluatePollState(&HasTTYReadableWait);
    if (ReadyCount < 0)
    {
        return ReadyCount;
    }

    if (ReadyCount > 0)
    {
        return ReadyCount;
    }

    if (TimeoutMilliseconds == 0)
    {
        return 0;
    }

    if (!HasTTYReadableWait)
    {
        if (TimeoutMilliseconds > 0)
        {
            uint64_t SleepTicks = static_cast<uint64_t>((TimeoutMilliseconds + 9) / 10);
            if (SleepTicks == 0)
            {
                SleepTicks = 1;
            }
            Logic->SleepProcess(CurrentProcess->Id, SleepTicks);
        }
        return 0;
    }

    if (TimeoutMilliseconds < 0)
    {
        while (true)
        {
            Logic->BlockProcessForTTYInput(CurrentProcess->Id);

            bool DummyTTYWaitFlag = false;
            ReadyCount            = EvaluatePollState(&DummyTTYWaitFlag);
            if (ReadyCount != 0)
            {
                return ReadyCount;
            }
        }
    }

    int64_t RemainingMilliseconds = TimeoutMilliseconds;
    while (RemainingMilliseconds > 0)
    {
        Logic->SleepProcess(CurrentProcess->Id, 1);

        bool DummyTTYWaitFlag = false;
        ReadyCount            = EvaluatePollState(&DummyTTYWaitFlag);
        if (ReadyCount != 0)
        {
            return ReadyCount;
        }

        RemainingMilliseconds -= 10;
    }

    return 0;
}

int64_t TranslationLayer::HandleEpollCreate1SystemCall(int64_t Flags)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    SynchronizationManager* Sync = Logic->GetSynchronizationManager();
    if (Sync == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if ((Flags & ~LINUX_EPOLL_CLOEXEC) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    for (size_t FileDescriptor = 0; FileDescriptor < MAX_OPEN_FILES_PER_PROCESS; ++FileDescriptor)
    {
        if (CurrentProcess->FileTable[FileDescriptor] != nullptr)
        {
            continue;
        }

        if (Sync->HasEventQueue(CurrentProcess->Id, FileDescriptor))
        {
            continue;
        }

        File* NewFile = new File;
        if (NewFile == nullptr)
        {
            return LINUX_ERR_ENOMEM;
        }

        NewFile->FileDescriptor  = FileDescriptor;
        NewFile->Node            = nullptr;
        NewFile->CurrentOffset   = 0;
        NewFile->AccessFlags     = READ_WRITE;
        NewFile->OpenFlags       = static_cast<uint64_t>(LINUX_O_ACCMODE);
        NewFile->DescriptorFlags = ((Flags & LINUX_EPOLL_CLOEXEC) != 0) ? LINUX_FD_CLOEXEC : 0;
        NewFile->DirectoryEntry  = nullptr;

        if (!Sync->CreateEventQueue(CurrentProcess->Id, FileDescriptor, static_cast<uint64_t>(Flags)))
        {
            delete NewFile;
            continue;
        }

        EventQueueKernelObject* EventQueue = Sync->GetEventQueue(CurrentProcess->Id, FileDescriptor);
        if (EventQueue == nullptr || EventQueue->Node == nullptr)
        {
            Sync->RemoveEventQueue(CurrentProcess->Id, FileDescriptor);
            delete NewFile;
            return LINUX_ERR_ENOMEM;
        }

        NewFile->Node = EventQueue->Node;

        CurrentProcess->FileTable[FileDescriptor] = NewFile;
        return static_cast<int64_t>(FileDescriptor);
    }

    return LINUX_ERR_EMFILE;
}

int64_t TranslationLayer::HandleEpollCtlSystemCall(uint64_t EpollFileDescriptor, int64_t Operation, uint64_t TargetFileDescriptor, const void* Event)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    SynchronizationManager* Sync = Logic->GetSynchronizationManager();
    if (Sync == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (EpollFileDescriptor >= MAX_OPEN_FILES_PER_PROCESS || TargetFileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* EpollFile = CurrentProcess->FileTable[EpollFileDescriptor];
    if (EpollFile == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (!Sync->HasEventQueue(CurrentProcess->Id, EpollFileDescriptor))
    {
        return LINUX_ERR_EINVAL;
    }

    File* TargetFile = CurrentProcess->FileTable[TargetFileDescriptor];
    if (TargetFile == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (Operation != LINUX_EPOLL_CTL_DEL && Event == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    LinuxEpollEvent KernelEvent = {};
    if (Operation != LINUX_EPOLL_CTL_DEL)
    {
        if (!Logic->CopyFromUserToKernel(Event, &KernelEvent, sizeof(KernelEvent)))
        {
            return LINUX_ERR_EFAULT;
        }

        if ((KernelEvent.Events & ~LINUX_EPOLL_SUPPORTED_EVENTS) != 0)
        {
            return LINUX_ERR_EINVAL;
        }
    }

    return Sync->ControlEventQueue(CurrentProcess->Id,
                                   EpollFileDescriptor,
                                   static_cast<int32_t>(Operation),
                                   TargetFileDescriptor,
                                   KernelEvent.Events,
                                   KernelEvent.Data);
}

int64_t TranslationLayer::HandleIoctlSystemCall(uint64_t FileDescriptor, uint64_t Request, uint64_t Argument)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->Node->FileOps == nullptr || OpenFile->Node->FileOps->Ioctl == nullptr)
    {
        return LINUX_ERR_ENOTTY;
    }

    return OpenFile->Node->FileOps->Ioctl(OpenFile, Request, Argument, Logic, CurrentProcess);
}

int64_t TranslationLayer::HandleSocketSystemCall(int64_t Domain, int64_t Type, int64_t Protocol)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    InterProcessComunicationManager* IPC = Logic->GetInterProcessComunicationManager();
    if (IPC == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    int64_t SocketType  = (Type & LINUX_SOCKET_CALL_TYPE_MASK);
    int64_t SocketFlags = (Type & ~LINUX_SOCKET_CALL_TYPE_MASK);
    if ((SocketFlags & ~(LINUX_SOCKET_CALL_NONBLOCK | LINUX_SOCKET_CALL_CLOEXEC)) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    int64_t FileDescriptor = -1;
    for (size_t FileIndex = 0; FileIndex < MAX_OPEN_FILES_PER_PROCESS; ++FileIndex)
    {
        if (CurrentProcess->FileTable[FileIndex] == nullptr)
        {
            FileDescriptor = static_cast<int64_t>(FileIndex);
            break;
        }
    }

    if (FileDescriptor < 0)
    {
        return LINUX_ERR_EMFILE;
    }

    int64_t SocketError = 0;
    Socket* NewSocket   = IPC->CreateSocket(Domain, SocketType, Protocol, CurrentProcess, FileDescriptor, &SocketError);
    if (NewSocket == nullptr)
    {
        if (SocketError == LINUX_ERR_EAFNOSUPPORT)
        {
            return LINUX_ERR_EAFNOSUPPORT;
        }

        if (SocketError == LINUX_ERR_ESOCKTNOSUPPORT)
        {
            return LINUX_ERR_ESOCKTNOSUPPORT;
        }

        if (SocketError != 0)
        {
            return SocketError;
        }

        return LINUX_ERR_ENOMEM;
    }

    INode* SocketNode = new INode;
    if (SocketNode == nullptr)
    {
        IPC->CloseSocket(CurrentProcess, FileDescriptor);
        return LINUX_ERR_ENOMEM;
    }

    *SocketNode                 = {};
    SocketNode->NodeType        = INODE_DEV;
    SocketNode->NodeSize        = 0;
    SocketNode->NodeData        = NewSocket;
    SocketNode->INodeOps        = nullptr;
    SocketNode->FileOps         = &NewSocket->FileOps;

    File* OpenFile = new File;
    if (OpenFile == nullptr)
    {
        delete SocketNode;
        IPC->CloseSocket(CurrentProcess, FileDescriptor);
        return LINUX_ERR_ENOMEM;
    }

    OpenFile->FileDescriptor  = static_cast<uint64_t>(FileDescriptor);
    OpenFile->Node            = SocketNode;
    OpenFile->CurrentOffset   = 0;
    OpenFile->AccessFlags     = READ_WRITE;
    OpenFile->OpenFlags       = static_cast<uint64_t>(LINUX_O_ACCMODE | ((SocketFlags & LINUX_SOCKET_CALL_NONBLOCK) != 0 ? LINUX_O_NONBLOCK : 0));
    OpenFile->DescriptorFlags = ((SocketFlags & LINUX_SOCKET_CALL_CLOEXEC) != 0) ? LINUX_FD_CLOEXEC : 0;
    OpenFile->DirectoryEntry  = nullptr;

    CurrentProcess->FileTable[FileDescriptor] = OpenFile;
    return FileDescriptor;
}

int64_t TranslationLayer::HandleConnectSystemCall(uint64_t FileDescriptor, const void* SocketAddress, uint64_t SocketAddressLength)
{
    if (Logic == nullptr || SocketAddress == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (SocketAddressLength == 0 || SocketAddressLength > SYSCALL_MAX_SOCKET_ADDRESS)
    {
        return LINUX_ERR_EINVAL;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    InterProcessComunicationManager* IPC = Logic->GetInterProcessComunicationManager();
    if (IPC == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    uint8_t KernelSocketAddress[SYSCALL_MAX_SOCKET_ADDRESS] = {};
    if (!Logic->CopyFromUserToKernel(SocketAddress, KernelSocketAddress, SocketAddressLength))
    {
        return LINUX_ERR_EFAULT;
    }

    int64_t ConnectResult = IPC->ConnectSocket(CurrentProcess, static_cast<int64_t>(FileDescriptor), KernelSocketAddress, SocketAddressLength);
    if (ConnectResult == LINUX_SOCKET_ERR_ENOTSOCK)
    {
        return LINUX_ERR_ENOTSOCK;
    }

    return ConnectResult;
}

int64_t TranslationLayer::HandleAcceptSystemCall(uint64_t FileDescriptor, void* SocketAddress, void* SocketAddressLength)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    InterProcessComunicationManager* IPC = Logic->GetInterProcessComunicationManager();
    if (IPC == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* ListeningFile = CurrentProcess->FileTable[FileDescriptor];
    if (ListeningFile == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if ((SocketAddress == nullptr) != (SocketAddressLength == nullptr))
    {
        return LINUX_ERR_EFAULT;
    }

    if (SocketAddressLength != nullptr)
    {
        uint32_t UserLength = 0;
        if (!Logic->CopyFromUserToKernel(SocketAddressLength, &UserLength, sizeof(UserLength)))
        {
            return LINUX_ERR_EFAULT;
        }

        UserLength = 0;
        if (!Logic->CopyFromKernelToUser(&UserLength, SocketAddressLength, sizeof(UserLength)))
        {
            return LINUX_ERR_EFAULT;
        }
    }

    int64_t NewFileDescriptor = -1;
    for (size_t Index = 0; Index < MAX_OPEN_FILES_PER_PROCESS; ++Index)
    {
        if (CurrentProcess->FileTable[Index] == nullptr)
        {
            NewFileDescriptor = static_cast<int64_t>(Index);
            break;
        }
    }

    if (NewFileDescriptor < 0)
    {
        return LINUX_ERR_EMFILE;
    }

    int64_t AcceptError = 0;
    Socket* AcceptedSocket = IPC->AcceptSocket(CurrentProcess, static_cast<int64_t>(FileDescriptor), CurrentProcess, NewFileDescriptor, &AcceptError);
    if (AcceptedSocket == nullptr)
    {
        if (AcceptError == LINUX_SOCKET_ERR_ENOTSOCK)
        {
            return LINUX_ERR_ENOTSOCK;
        }

        if (AcceptError == LINUX_SOCKET_ERR_EAGAIN && (ListeningFile->OpenFlags & LINUX_O_NONBLOCK) == 0)
        {
            return LINUX_ERR_EAGAIN;
        }

        if (AcceptError != 0)
        {
            return AcceptError;
        }

        return LINUX_ERR_EAGAIN;
    }

    INode* SocketNode = new INode;
    if (SocketNode == nullptr)
    {
        IPC->CloseSocket(CurrentProcess, NewFileDescriptor);
        return LINUX_ERR_ENOMEM;
    }

    *SocketNode                 = {};
    SocketNode->NodeType        = INODE_DEV;
    SocketNode->NodeSize        = 0;
    SocketNode->NodeData        = AcceptedSocket;
    SocketNode->INodeOps        = nullptr;
    SocketNode->FileOps         = &AcceptedSocket->FileOps;

    File* AcceptedFile = new File;
    if (AcceptedFile == nullptr)
    {
        delete SocketNode;
        IPC->CloseSocket(CurrentProcess, NewFileDescriptor);
        return LINUX_ERR_ENOMEM;
    }

    AcceptedFile->FileDescriptor  = static_cast<uint64_t>(NewFileDescriptor);
    AcceptedFile->Node            = SocketNode;
    AcceptedFile->CurrentOffset   = 0;
    AcceptedFile->AccessFlags     = READ_WRITE;
    AcceptedFile->OpenFlags       = LINUX_O_ACCMODE;
    AcceptedFile->DescriptorFlags = 0;
    AcceptedFile->DirectoryEntry  = nullptr;

    CurrentProcess->FileTable[NewFileDescriptor] = AcceptedFile;
    return NewFileDescriptor;
}

int64_t TranslationLayer::HandleShutdownSystemCall(uint64_t FileDescriptor, int64_t How)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    InterProcessComunicationManager* IPC = Logic->GetInterProcessComunicationManager();
    if (IPC == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    int64_t ShutdownResult = IPC->ShutdownSocket(CurrentProcess, static_cast<int64_t>(FileDescriptor), How);
    if (ShutdownResult == LINUX_SOCKET_ERR_ENOTSOCK)
    {
        return LINUX_ERR_ENOTSOCK;
    }

    if (ShutdownResult == LINUX_SOCKET_ERR_ENOTCONN)
    {
        return LINUX_ERR_ENOTCONN;
    }

    return ShutdownResult;
}

int64_t TranslationLayer::HandleBindSystemCall(uint64_t FileDescriptor, const void* SocketAddress, uint64_t SocketAddressLength)
{
    if (Logic == nullptr || SocketAddress == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (SocketAddressLength == 0 || SocketAddressLength > SYSCALL_MAX_SOCKET_ADDRESS)
    {
        return LINUX_ERR_EINVAL;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    InterProcessComunicationManager* IPC = Logic->GetInterProcessComunicationManager();
    if (IPC == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    uint8_t KernelSocketAddress[SYSCALL_MAX_SOCKET_ADDRESS] = {};
    if (!Logic->CopyFromUserToKernel(SocketAddress, KernelSocketAddress, SocketAddressLength))
    {
        return LINUX_ERR_EFAULT;
    }

    int64_t BindResult = IPC->BindSocket(CurrentProcess, static_cast<int64_t>(FileDescriptor), KernelSocketAddress, SocketAddressLength);
    if (BindResult == LINUX_SOCKET_ERR_ENOTSOCK)
    {
        return LINUX_ERR_ENOTSOCK;
    }

    return BindResult;
}

int64_t TranslationLayer::HandleListenSystemCall(uint64_t FileDescriptor, int64_t Backlog)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    InterProcessComunicationManager* IPC = Logic->GetInterProcessComunicationManager();
    if (IPC == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    int64_t ListenResult = IPC->ListenSocket(CurrentProcess, static_cast<int64_t>(FileDescriptor), Backlog);
    if (ListenResult == LINUX_SOCKET_ERR_ENOTSOCK)
    {
        return LINUX_ERR_ENOTSOCK;
    }

    return ListenResult;
}

int64_t TranslationLayer::HandleGetsocknameSystemCall(uint64_t FileDescriptor, void* SocketAddress, void* SocketAddressLength)
{
    if (Logic == nullptr || SocketAddress == nullptr || SocketAddressLength == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr || OpenFile->Node->NodeData == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    Socket* SocketEntry = reinterpret_cast<Socket*>(OpenFile->Node->NodeData);
    if (SocketEntry == nullptr)
    {
        return LINUX_ERR_ENOTSOCK;
    }

    uint32_t UserLength = 0;
    if (!Logic->CopyFromUserToKernel(SocketAddressLength, &UserLength, sizeof(UserLength)))
    {
        return LINUX_ERR_EFAULT;
    }

    uint8_t  KernelSocketAddress[SYSCALL_MAX_SOCKET_ADDRESS] = {};
    uint32_t RequiredLength = 0;

    if (SocketEntry->Domain == LINUX_AF_UNIX)
    {
        RequiredLength = static_cast<uint32_t>(sizeof(uint16_t));
        LinuxSockAddrUn* UnixAddress = reinterpret_cast<LinuxSockAddrUn*>(KernelSocketAddress);
        UnixAddress->Family = static_cast<uint16_t>(LINUX_AF_UNIX);

        UnixSocket* UnixImplementation = reinterpret_cast<UnixSocket*>(SocketEntry->Implementation);
        if (UnixImplementation != nullptr && UnixImplementation->IsBound && UnixImplementation->Path != nullptr)
        {
            uint64_t MaxPathBytes = sizeof(UnixAddress->Path);
            if (UnixImplementation->IsAbstract)
            {
                uint64_t CopyLength = UnixImplementation->PathLength;
                if (CopyLength > MaxPathBytes)
                {
                    CopyLength = MaxPathBytes;
                }

                for (uint64_t Index = 0; Index < CopyLength; ++Index)
                {
                    UnixAddress->Path[Index] = UnixImplementation->Path[Index];
                }

                RequiredLength = static_cast<uint32_t>(sizeof(uint16_t) + CopyLength);
            }
            else
            {
                uint64_t CopyLength = UnixImplementation->PathLength;
                if (MaxPathBytes > 0 && CopyLength > (MaxPathBytes - 1))
                {
                    CopyLength = (MaxPathBytes - 1);
                }

                for (uint64_t Index = 0; Index < CopyLength; ++Index)
                {
                    UnixAddress->Path[Index] = UnixImplementation->Path[Index];
                }

                if (MaxPathBytes > 0)
                {
                    UnixAddress->Path[CopyLength] = '\0';
                    RequiredLength = static_cast<uint32_t>(sizeof(uint16_t) + CopyLength + 1);
                }
            }
        }
    }
    else if (SocketEntry->Domain == LINUX_AF_INET)
    {
        LinuxSockAddrIn* InternetAddress = reinterpret_cast<LinuxSockAddrIn*>(KernelSocketAddress);
        InternetAddress->Family = static_cast<uint16_t>(LINUX_AF_INET);
        InternetAddress->Port   = 0;
        InternetAddress->Address = 0;
        for (size_t Index = 0; Index < sizeof(InternetAddress->Zero); ++Index)
        {
            InternetAddress->Zero[Index] = 0;
        }

        NetworkSocket* NetworkImplementation = reinterpret_cast<NetworkSocket*>(SocketEntry->Implementation);
        if (NetworkImplementation != nullptr)
        {
            InternetAddress->Port    = NetworkImplementation->LocalPort;
            InternetAddress->Address = NetworkImplementation->LocalIp;
        }

        RequiredLength = static_cast<uint32_t>(sizeof(LinuxSockAddrIn));
    }
    else if (SocketEntry->Domain == LINUX_AF_NETLINK)
    {
        LinuxSockAddrNl* NetlinkAddress = reinterpret_cast<LinuxSockAddrNl*>(KernelSocketAddress);
        NetlinkAddress->Family = static_cast<uint16_t>(LINUX_AF_NETLINK);
        NetlinkAddress->Pad    = 0;
        NetlinkAddress->Pid    = 0;
        NetlinkAddress->Groups = 0;

        NetlinkSocket* NetlinkImplementation = reinterpret_cast<NetlinkSocket*>(SocketEntry->Implementation);
        if (NetlinkImplementation != nullptr)
        {
            NetlinkAddress->Pid    = NetlinkImplementation->LocalPid;
            NetlinkAddress->Groups = NetlinkImplementation->LocalGroups;
        }

        RequiredLength = static_cast<uint32_t>(sizeof(LinuxSockAddrNl));
    }
    else
    {
        return LINUX_ERR_EAFNOSUPPORT;
    }

    uint32_t CopyLength = (UserLength < RequiredLength) ? UserLength : RequiredLength;
    if (CopyLength > 0 && !Logic->CopyFromKernelToUser(KernelSocketAddress, SocketAddress, CopyLength))
    {
        return LINUX_ERR_EFAULT;
    }

    if (!Logic->CopyFromKernelToUser(&RequiredLength, SocketAddressLength, sizeof(RequiredLength)))
    {
        return LINUX_ERR_EFAULT;
    }

    return 0;
}

int64_t TranslationLayer::HandleSetsockoptSystemCall(uint64_t FileDescriptor, int64_t Level, int64_t OptionName, const void* OptionValue, uint64_t OptionLength)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr || OpenFile->Node->NodeData == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    Socket* SocketEntry = reinterpret_cast<Socket*>(OpenFile->Node->NodeData);
    if (SocketEntry == nullptr)
    {
        return LINUX_ERR_ENOTSOCK;
    }

    if (Level != LINUX_SOL_SOCKET)
    {
        return LINUX_ERR_ENOPROTOOPT;
    }

    switch (OptionName)
    {
        case LINUX_SO_REUSEADDR:
        case LINUX_SO_BROADCAST:
        case LINUX_SO_SNDBUF:
        case LINUX_SO_RCVBUF:
        case LINUX_SO_KEEPALIVE:
        case LINUX_SO_OOBINLINE:
        case LINUX_SO_REUSEPORT:
        case LINUX_SO_PASSCRED:
        case LINUX_SO_RCVLOWAT:
        case LINUX_SO_SNDLOWAT:
        {
            if (OptionValue == nullptr || OptionLength < sizeof(int32_t))
            {
                return LINUX_ERR_EINVAL;
            }

            int32_t IntegerOption = 0;
            if (!Logic->CopyFromUserToKernel(OptionValue, &IntegerOption, sizeof(IntegerOption)))
            {
                return LINUX_ERR_EFAULT;
            }

            (void) IntegerOption;
            return 0;
        }

        case LINUX_SO_RCVTIMEO:
        case LINUX_SO_SNDTIMEO:
        {
            if (OptionValue == nullptr || OptionLength < sizeof(LinuxTimeVal))
            {
                return LINUX_ERR_EINVAL;
            }

            LinuxTimeVal TimeValue = {};
            if (!Logic->CopyFromUserToKernel(OptionValue, &TimeValue, sizeof(TimeValue)))
            {
                return LINUX_ERR_EFAULT;
            }

            if (TimeValue.Seconds < 0 || TimeValue.Microseconds < 0)
            {
                return LINUX_ERR_EINVAL;
            }

            return 0;
        }

        case LINUX_SO_BINDTODEVICE:
        {
            if (OptionLength == 0)
            {
                return 0;
            }

            if (OptionValue == nullptr || OptionLength > SYSCALL_PATH_MAX)
            {
                return LINUX_ERR_EINVAL;
            }

            char DeviceName[SYSCALL_PATH_MAX] = {};
            if (!Logic->CopyFromUserToKernel(OptionValue, DeviceName, OptionLength))
            {
                return LINUX_ERR_EFAULT;
            }

            return 0;
        }

        default:
            return LINUX_ERR_ENOPROTOOPT;
    }
}

int64_t TranslationLayer::HandleGetsockoptSystemCall(uint64_t FileDescriptor, int64_t Level, int64_t OptionName, void* OptionValue, void* OptionLength)
{
    if (Logic == nullptr || OptionValue == nullptr || OptionLength == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr || OpenFile->Node->NodeData == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    Socket* SocketEntry = reinterpret_cast<Socket*>(OpenFile->Node->NodeData);
    if (SocketEntry == nullptr)
    {
        return LINUX_ERR_ENOTSOCK;
    }

    if (Level != LINUX_SOL_SOCKET)
    {
        return LINUX_ERR_ENOPROTOOPT;
    }

    int32_t  OptionValueResult = 0;
    uint32_t RequiredLength    = static_cast<uint32_t>(sizeof(OptionValueResult));

    switch (OptionName)
    {
        case LINUX_SO_TYPE:
            OptionValueResult = SocketEntry->Type;
            break;
        case LINUX_SO_ERROR:
            OptionValueResult = 0;
            break;
        case LINUX_SO_DOMAIN:
            OptionValueResult = SocketEntry->Domain;
            break;
        case LINUX_SO_PROTOCOL:
            OptionValueResult = SocketEntry->Protocol;
            break;
        case LINUX_SO_ACCEPTCONN:
        {
            bool IsListening = false;
            if (SocketEntry->Domain == LINUX_AF_UNIX)
            {
                UnixSocket* UnixImplementation = reinterpret_cast<UnixSocket*>(SocketEntry->Implementation);
                IsListening                    = (UnixImplementation != nullptr && UnixImplementation->IsListening);
            }
            else if (SocketEntry->Domain == LINUX_AF_INET)
            {
                NetworkSocket* NetworkImplementation = reinterpret_cast<NetworkSocket*>(SocketEntry->Implementation);
                IsListening                          = (NetworkImplementation != nullptr && NetworkImplementation->IsListening);
            }

            OptionValueResult = IsListening ? 1 : 0;
            break;
        }
        default:
            return LINUX_ERR_ENOPROTOOPT;
    }

    uint32_t UserLength = 0;
    if (!Logic->CopyFromUserToKernel(OptionLength, &UserLength, sizeof(UserLength)))
    {
        return LINUX_ERR_EFAULT;
    }

    uint32_t CopyLength = (UserLength < RequiredLength) ? UserLength : RequiredLength;
    if (CopyLength > 0 && !Logic->CopyFromKernelToUser(&OptionValueResult, OptionValue, CopyLength))
    {
        return LINUX_ERR_EFAULT;
    }

    if (!Logic->CopyFromKernelToUser(&RequiredLength, OptionLength, sizeof(RequiredLength)))
    {
        return LINUX_ERR_EFAULT;
    }

    return 0;
}

int64_t TranslationLayer::HandleOpenSystemCall(const char* Path, uint64_t Flags)
{
    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char        EffectivePath[SYSCALL_PATH_MAX] = {};
    const char* LookupPath                      = KernelPath;

    if (KernelPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectivePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        LookupPath                             = EffectivePath;
    }

    Dentry* NodeDentry = VFS->Lookup(LookupPath);
    if (NodeDentry != nullptr && NodeDentry->inode != nullptr && (Flags & LINUX_O_CREAT) != 0 && (Flags & LINUX_O_EXCL) != 0)
    {
        return LINUX_ERR_EEXIST;
    }

    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        if ((Flags & LINUX_O_CREAT) == 0)
        {
            return LINUX_ERR_ENOENT;
        }

        if (!VFS->CreateFile(LookupPath, INODE_FILE))
        {
            return LINUX_ERR_ENOENT;
        }

        NodeDentry = VFS->Lookup(LookupPath);
        if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }
    }

    return AllocateProcessFileDescriptor(CurrentProcess, NodeDentry, Flags);
}

int64_t TranslationLayer::HandleOpenAtSystemCall(int64_t DirectoryFileDescriptor, const char* Path, uint64_t Flags, uint64_t Mode)
{
    (void) Mode;

    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    if (KernelPath[0] == '\0')
    {
        return LINUX_ERR_ENOENT;
    }

    Dentry*     NodeDentry = nullptr;
    const char* LookupPath = nullptr;

    if (KernelPath[0] == '/')
    {
        LookupPath = KernelPath;
        NodeDentry = VFS->Lookup(LookupPath);

        if (NodeDentry != nullptr && NodeDentry->inode != nullptr && (Flags & LINUX_O_CREAT) != 0 && (Flags & LINUX_O_EXCL) != 0)
        {
            return LINUX_ERR_EEXIST;
        }
    }
    else
    {
        Dentry* BaseDirectory = nullptr;

        if (DirectoryFileDescriptor == LINUX_AT_FDCWD)
        {
            BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        }
        else
        {
            if (DirectoryFileDescriptor < 0 || static_cast<uint64_t>(DirectoryFileDescriptor) >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* DirectoryFile = CurrentProcess->FileTable[static_cast<size_t>(DirectoryFileDescriptor)];
            if (DirectoryFile == nullptr || DirectoryFile->Node == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            if (DirectoryFile->Node->NodeType != INODE_DIR)
            {
                return LINUX_ERR_ENOTDIR;
            }

            BaseDirectory = DirectoryFile->DirectoryEntry;
        }

        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        char     AbsolutePath[SYSCALL_PATH_MAX] = {};
        uint64_t BasePathLength                 = CStrLength(BasePath);
        uint64_t RelativeLength                 = CStrLength(KernelPath);
        uint64_t NeedsSeparator                 = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes                  = BasePathLength + NeedsSeparator + RelativeLength + 1;

        if (RequiredBytes > sizeof(AbsolutePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(AbsolutePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            AbsolutePath[Cursor++] = '/';
        }

        memcpy(AbsolutePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        AbsolutePath[Cursor + RelativeLength] = '\0';

        LookupPath = AbsolutePath;
        NodeDentry = VFS->Lookup(LookupPath);

        if (NodeDentry != nullptr && NodeDentry->inode != nullptr && (Flags & LINUX_O_CREAT) != 0 && (Flags & LINUX_O_EXCL) != 0)
        {
            return LINUX_ERR_EEXIST;
        }

        if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
        {
            if ((Flags & LINUX_O_CREAT) == 0)
            {
                return LINUX_ERR_ENOENT;
            }

            if (!VFS->CreateFile(LookupPath, INODE_FILE))
            {
                return LINUX_ERR_ENOENT;
            }

            NodeDentry = VFS->Lookup(LookupPath);
        }

        if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        return AllocateProcessFileDescriptor(CurrentProcess, NodeDentry, Flags);
    }

    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        if ((Flags & LINUX_O_CREAT) == 0)
        {
            return LINUX_ERR_ENOENT;
        }

        if (LookupPath == nullptr || !VFS->CreateFile(LookupPath, INODE_FILE))
        {
            return LINUX_ERR_ENOENT;
        }

        NodeDentry = VFS->Lookup(LookupPath);
        if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        return AllocateProcessFileDescriptor(CurrentProcess, NodeDentry, Flags);
    }

    return AllocateProcessFileDescriptor(CurrentProcess, NodeDentry, Flags);
}

int64_t TranslationLayer::HandleStatSystemCall(const char* Path, void* Buffer)
{
    if (Logic == nullptr || Path == nullptr || Buffer == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    if (VFS == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char        EffectivePath[SYSCALL_PATH_MAX] = {};
    const char* LookupPath                      = KernelPath;

    if (KernelPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectivePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        LookupPath                             = EffectivePath;
    }

    Dentry* NodeDentry = VFS->Lookup(LookupPath);
    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    LinuxStat KernelStat = {};
    PopulateLinuxStatFromNode(NodeDentry->inode, &KernelStat);

    if (!Logic->CopyFromKernelToUser(&KernelStat, Buffer, sizeof(KernelStat)))
    {
        return LINUX_ERR_EFAULT;
    }

    return 0;
}

int64_t TranslationLayer::HandleFstatSystemCall(uint64_t FileDescriptor, void* Buffer)
{
    if (Logic == nullptr || Buffer == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    LinuxStat KernelStat = {};
    PopulateLinuxStatFromNode(OpenFile->Node, &KernelStat);

    if (!Logic->CopyFromKernelToUser(&KernelStat, Buffer, sizeof(KernelStat)))
    {
        return LINUX_ERR_EFAULT;
    }

    return 0;
}

int64_t TranslationLayer::HandleLstatSystemCall(const char* Path, void* Buffer)
{
    if (Logic == nullptr || Path == nullptr || Buffer == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    if (VFS == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char        EffectivePath[SYSCALL_PATH_MAX] = {};
    const char* LookupPath                      = KernelPath;

    if (KernelPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectivePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        LookupPath                             = EffectivePath;
    }

    Dentry* NodeDentry = VFS->LookupNoFollowFinal(LookupPath);
    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    LinuxStat KernelStat = {};
    PopulateLinuxStatFromNode(NodeDentry->inode, &KernelStat);

    if (!Logic->CopyFromKernelToUser(&KernelStat, Buffer, sizeof(KernelStat)))
    {
        return LINUX_ERR_EFAULT;
    }

    return 0;
}

int64_t TranslationLayer::HandleNewFstatatSystemCall(int64_t DirectoryFileDescriptor, const char* Path, void* Buffer, int64_t Flags)
{
    if (Logic == nullptr || Buffer == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    int64_t AllowedFlags = LINUX_AT_SYMLINK_NOFOLLOW | LINUX_AT_NO_AUTOMOUNT | LINUX_AT_EMPTY_PATH;
    if ((Flags & ~AllowedFlags) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    Dentry* NodeDentry = nullptr;

    if (KernelPath[0] == '\0' && (Flags & LINUX_AT_EMPTY_PATH) != 0)
    {
        if (DirectoryFileDescriptor == LINUX_AT_FDCWD)
        {
            NodeDentry = CurrentProcess->CurrentFileSystemLocation;
        }
        else
        {
            if (DirectoryFileDescriptor < 0 || static_cast<uint64_t>(DirectoryFileDescriptor) >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* ExistingFile = CurrentProcess->FileTable[static_cast<size_t>(DirectoryFileDescriptor)];
            if (ExistingFile == nullptr || ExistingFile->Node == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            NodeDentry = ExistingFile->DirectoryEntry;
            if (NodeDentry == nullptr)
            {
                return LINUX_ERR_EFAULT;
            }
        }
    }
    else
    {
        if (KernelPath[0] == '\0')
        {
            return LINUX_ERR_ENOENT;
        }

        if (KernelPath[0] == '/')
        {
            NodeDentry = ((Flags & LINUX_AT_SYMLINK_NOFOLLOW) != 0) ? VFS->LookupNoFollowFinal(KernelPath) : VFS->Lookup(KernelPath);
        }
        else
        {
            Dentry* BaseDirectory = nullptr;

            if (DirectoryFileDescriptor == LINUX_AT_FDCWD)
            {
                BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
            }
            else
            {
                if (DirectoryFileDescriptor < 0 || static_cast<uint64_t>(DirectoryFileDescriptor) >= MAX_OPEN_FILES_PER_PROCESS)
                {
                    return LINUX_ERR_EBADF;
                }

                File* DirectoryFile = CurrentProcess->FileTable[static_cast<size_t>(DirectoryFileDescriptor)];
                if (DirectoryFile == nullptr || DirectoryFile->Node == nullptr)
                {
                    return LINUX_ERR_EBADF;
                }

                if (DirectoryFile->Node->NodeType != INODE_DIR)
                {
                    return LINUX_ERR_ENOTDIR;
                }

                BaseDirectory = DirectoryFile->DirectoryEntry;
            }

            if (BaseDirectory == nullptr)
            {
                return LINUX_ERR_ENOENT;
            }

            bool FollowFinalSymlink = ((Flags & LINUX_AT_SYMLINK_NOFOLLOW) == 0);
            NodeDentry              = ResolveSimpleRelativeChildDentry(BaseDirectory, KernelPath, FollowFinalSymlink);
            if (NodeDentry != nullptr)
            {
                goto finalize_newfstatat_lookup;
            }

            char BasePath[SYSCALL_PATH_MAX] = {};
            if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
            {
                return LINUX_ERR_EFAULT;
            }

            char     AbsolutePath[SYSCALL_PATH_MAX] = {};
            uint64_t BasePathLength                 = CStrLength(BasePath);
            uint64_t RelativeLength                 = CStrLength(KernelPath);
            uint64_t NeedsSeparator                 = (BasePathLength > 1) ? 1 : 0;
            uint64_t RequiredBytes                  = BasePathLength + NeedsSeparator + RelativeLength + 1;

            if (RequiredBytes > sizeof(AbsolutePath))
            {
                return LINUX_ERR_EINVAL;
            }

            memcpy(AbsolutePath, BasePath, static_cast<size_t>(BasePathLength));
            uint64_t Cursor = BasePathLength;
            if (NeedsSeparator != 0)
            {
                AbsolutePath[Cursor++] = '/';
            }

            memcpy(AbsolutePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
            AbsolutePath[Cursor + RelativeLength] = '\0';

            NodeDentry = ((Flags & LINUX_AT_SYMLINK_NOFOLLOW) != 0) ? VFS->LookupNoFollowFinal(AbsolutePath) : VFS->Lookup(AbsolutePath);
        }
    }

finalize_newfstatat_lookup:

    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    LinuxStat KernelStat = {};
    PopulateLinuxStatFromNode(NodeDentry->inode, &KernelStat);
    if (!Logic->CopyFromKernelToUser(&KernelStat, Buffer, sizeof(KernelStat)))
    {
        return LINUX_ERR_EFAULT;
    }

    return 0;
}

int64_t TranslationLayer::HandleGetdents64SystemCall(uint64_t FileDescriptor, void* Buffer, uint64_t BufferSize)
{
    if (Logic == nullptr || (Buffer == nullptr && BufferSize != 0))
    {
        return LINUX_ERR_EFAULT;
    }

    if (BufferSize == 0)
    {
        return 0;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->AccessFlags == WRITE)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->Node->NodeType != INODE_DIR || OpenFile->DirectoryEntry == nullptr)
    {
        return LINUX_ERR_ENOTDIR;
    }

    uint8_t  StackBuffer[SYSCALL_COPY_CHUNK_SIZE];
    bool     UseHeapBuffer = (BufferSize > SYSCALL_COPY_CHUNK_SIZE);
    uint8_t* KernelBuffer  = UseHeapBuffer ? reinterpret_cast<uint8_t*>(Logic->kmalloc(BufferSize)) : StackBuffer;
    if (KernelBuffer == nullptr)
    {
        return LINUX_ERR_ENOMEM;
    }

    uint64_t Cursor          = 0;
    uint64_t EntryIndex      = OpenFile->CurrentOffset;
    Dentry*  DirectoryDentry = OpenFile->DirectoryEntry;
    uint64_t TotalEntries    = DirectoryDentry->child_count + 2;

    while (EntryIndex < TotalEntries)
    {
        const char* EntryName = nullptr;
        INode*      EntryNode = nullptr;

        if (EntryIndex == 0)
        {
            EntryName = ".";
            EntryNode = DirectoryDentry->inode;
        }
        else if (EntryIndex == 1)
        {
            EntryName = "..";
            EntryNode = (DirectoryDentry->parent != nullptr && DirectoryDentry->parent->inode != nullptr) ? DirectoryDentry->parent->inode : DirectoryDentry->inode;
        }
        else
        {
            uint64_t ChildIndex = EntryIndex - 2;
            Dentry*  Child      = DirectoryDentry->children[ChildIndex];
            if (Child == nullptr || Child->inode == nullptr || Child->name == nullptr)
            {
                ++EntryIndex;
                continue;
            }

            EntryName = Child->name;
            EntryNode = Child->inode;
        }

        uint64_t NameLength    = CStrLength(EntryName);
        uint64_t MinimumRecord = sizeof(LinuxDirent64Header) + NameLength + 1;
        uint64_t RecordLength  = AlignUpValue(MinimumRecord, 8);

        if (RecordLength > BufferSize)
        {
            if (UseHeapBuffer)
            {
                Logic->kfree(KernelBuffer);
            }
            return LINUX_ERR_EINVAL;
        }

        if ((Cursor + RecordLength) > BufferSize)
        {
            break;
        }

        LinuxDirent64Header Header = {};
        Header.Inode               = reinterpret_cast<uint64_t>(EntryNode);
        Header.Offset              = EntryIndex + 1;
        Header.RecordLength        = static_cast<uint16_t>(RecordLength);
        Header.Type                = BuildLinuxDirentTypeFromNode(EntryNode);

        memcpy(KernelBuffer + Cursor, &Header, sizeof(Header));
        memcpy(KernelBuffer + Cursor + sizeof(Header), EntryName, static_cast<size_t>(NameLength));
        KernelBuffer[Cursor + sizeof(Header) + NameLength] = '\0';

        uint64_t PaddingStart = Cursor + sizeof(Header) + NameLength + 1;
        for (uint64_t PaddingIndex = PaddingStart; PaddingIndex < (Cursor + RecordLength); ++PaddingIndex)
        {
            KernelBuffer[PaddingIndex] = 0;
        }

        Cursor += RecordLength;
        ++EntryIndex;
    }

    if (Cursor == 0)
    {
        if (UseHeapBuffer)
        {
            Logic->kfree(KernelBuffer);
        }
        return 0;
    }

    if (!Logic->CopyFromKernelToUser(KernelBuffer, Buffer, Cursor))
    {
        if (UseHeapBuffer)
        {
            Logic->kfree(KernelBuffer);
        }
        return LINUX_ERR_EFAULT;
    }

    OpenFile->CurrentOffset = EntryIndex;

    if (UseHeapBuffer)
    {
        Logic->kfree(KernelBuffer);
    }
    return static_cast<int64_t>(Cursor);
}

int64_t TranslationLayer::HandleFcntlSystemCall(uint64_t FileDescriptor, uint64_t Command, uint64_t Argument)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    auto DuplicateFromMinimum = [&](uint64_t MinimumFileDescriptor, bool SetCloseOnExec) -> int64_t
    {
        if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
        {
            return LINUX_ERR_EBADF;
        }

        File* SourceFile = CurrentProcess->FileTable[FileDescriptor];
        if (SourceFile == nullptr)
        {
            return LINUX_ERR_EBADF;
        }

        if (MinimumFileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
        {
            return LINUX_ERR_EINVAL;
        }

        for (size_t CandidateDescriptor = static_cast<size_t>(MinimumFileDescriptor); CandidateDescriptor < MAX_OPEN_FILES_PER_PROCESS; ++CandidateDescriptor)
        {
            if (CurrentProcess->FileTable[CandidateDescriptor] != nullptr)
            {
                continue;
            }

            File* DuplicatedFile = new File;
            if (DuplicatedFile == nullptr)
            {
                return LINUX_ERR_ENOMEM;
            }

            *DuplicatedFile                 = *SourceFile;
            DuplicatedFile->FileDescriptor  = CandidateDescriptor;
            DuplicatedFile->DescriptorFlags = SetCloseOnExec ? LINUX_FD_CLOEXEC : 0;

            CurrentProcess->FileTable[CandidateDescriptor] = DuplicatedFile;
            return static_cast<int64_t>(CandidateDescriptor);
        }

        return LINUX_ERR_EMFILE;
    };

    switch (Command)
    {
        case LINUX_F_DUPFD:
            return DuplicateFromMinimum(Argument, false);
        case LINUX_F_DUPFD_CLOEXEC:
            return DuplicateFromMinimum(Argument, true);
        case LINUX_F_GETFD:
        {
            if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
            if (OpenFile == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            return static_cast<int64_t>(OpenFile->DescriptorFlags & LINUX_FD_CLOEXEC);
        }
        case LINUX_F_SETFD:
        {
            if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
            if (OpenFile == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            OpenFile->DescriptorFlags = (Argument & LINUX_FD_CLOEXEC);
            return 0;
        }
        case LINUX_F_GETFL:
        {
            if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
            if (OpenFile == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            return static_cast<int64_t>(OpenFile->OpenFlags);
        }
        case LINUX_F_SETFL:
        {
            if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
            if (OpenFile == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            uint64_t PreservedFlags = OpenFile->OpenFlags & ~LINUX_FCNTL_SETFL_ALLOWED;
            uint64_t RequestedFlags = Argument & LINUX_FCNTL_SETFL_ALLOWED;
            OpenFile->OpenFlags     = (PreservedFlags | RequestedFlags);
            return 0;
        }
        default:
            return LINUX_ERR_ENOSYS;
    }
}

int64_t TranslationLayer::HandleFchmodSystemCall(uint64_t FileDescriptor, uint64_t Mode)
{
    (void) Mode;

    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    return 0;
}

int64_t TranslationLayer::HandleChmodSystemCall(const char* Path, uint64_t Mode)
{
    if (Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    int64_t FileDescriptor = HandleOpenAtSystemCall(LINUX_AT_FDCWD, Path, 0, 0);
    if (FileDescriptor < 0)
    {
        return FileDescriptor;
    }

    int64_t ChmodResult = HandleFchmodSystemCall(static_cast<uint64_t>(FileDescriptor), Mode);
    HandleCloseSystemCall(static_cast<uint64_t>(FileDescriptor));
    return ChmodResult;
}

int64_t TranslationLayer::HandleCloseSystemCall(uint64_t FileDescriptor)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    SynchronizationManager* Sync = Logic->GetSynchronizationManager();
    if (Sync != nullptr)
    {
        Sync->RemoveEventQueue(CurrentProcess->Id, FileDescriptor);
    }

    InterProcessComunicationManager* IPC = Logic->GetInterProcessComunicationManager();
    bool                              ClosedSocket = false;
    if (IPC != nullptr)
    {
        ClosedSocket = IPC->CloseSocket(CurrentProcess, static_cast<int64_t>(FileDescriptor));
    }

    if (ClosedSocket && OpenFile->Node != nullptr)
    {
        delete OpenFile->Node;
        OpenFile->Node = nullptr;
    }

    delete OpenFile;
    CurrentProcess->FileTable[FileDescriptor] = nullptr;
    return 0;
}

int64_t TranslationLayer::HandleGetcwdSystemCall(char* Buffer, uint64_t Size)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Buffer == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Size == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (CurrentProcess->CurrentFileSystemLocation == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    char KernelPathBuffer[SYSCALL_PATH_MAX] = {};
    if (!BuildAbsolutePathFromDentry(CurrentProcess->CurrentFileSystemLocation, KernelPathBuffer, sizeof(KernelPathBuffer)))
    {
        return LINUX_ERR_ENOENT;
    }

    uint64_t PathBytesWithNull = CStrLength(KernelPathBuffer) + 1;
    if (PathBytesWithNull > Size)
    {
        return LINUX_ERR_ERANGE;
    }

    if (!Logic->CopyFromKernelToUser(KernelPathBuffer, Buffer, PathBytesWithNull))
    {
        return LINUX_ERR_EFAULT;
    }

    return reinterpret_cast<int64_t>(Buffer);
}

int64_t TranslationLayer::HandleChdirSystemCall(const char* Path)
{
    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    if (KernelPath[0] == '\0')
    {
        return LINUX_ERR_ENOENT;
    }

    char        EffectivePath[SYSCALL_PATH_MAX] = {};
    const char* LookupPath                      = KernelPath;

    if (KernelPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectivePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        LookupPath                             = EffectivePath;
    }

    Dentry* NodeDentry = VFS->Lookup(LookupPath);
    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    if (NodeDentry->inode->NodeType != INODE_DIR)
    {
        return LINUX_ERR_ENOTDIR;
    }

    CurrentProcess->CurrentFileSystemLocation = NodeDentry;
    SyncVforkParentFileSystemLocation(PM, CurrentProcess);
    return 0;
}

int64_t TranslationLayer::HandleMkdirSystemCall(const char* Path, uint64_t Mode)
{
    (void) Mode;

    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    if (KernelPath[0] == '\0')
    {
        return LINUX_ERR_ENOENT;
    }

    char        EffectivePath[SYSCALL_PATH_MAX] = {};
    const char* CreatePath                      = KernelPath;

    if (KernelPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectivePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        CreatePath                             = EffectivePath;
    }

    Dentry* Existing = VFS->Lookup(CreatePath);
    if (Existing != nullptr)
    {
        return LINUX_ERR_EEXIST;
    }

    if (!VFS->CreateFile(CreatePath, INODE_DIR))
    {
        return LINUX_ERR_ENOENT;
    }

    return 0;
}

int64_t TranslationLayer::HandleAccessSystemCall(const char* Path, int64_t Mode)
{
    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if ((Mode & ~(LINUX_ACCESS_R_OK | LINUX_ACCESS_W_OK | LINUX_ACCESS_X_OK)) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    if (KernelPath[0] == '\0')
    {
        return LINUX_ERR_ENOENT;
    }

    char        EffectivePath[SYSCALL_PATH_MAX] = {};
    const char* LookupPath                      = KernelPath;

    if (KernelPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectivePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        LookupPath                             = EffectivePath;
    }

    Dentry* Existing = VFS->Lookup(LookupPath);
    if (Existing == nullptr || Existing->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    if (Mode == LINUX_ACCESS_F_OK)
    {
        return 0;
    }

    if ((Mode & (LINUX_ACCESS_R_OK | LINUX_ACCESS_W_OK | LINUX_ACCESS_X_OK)) != 0)
    {
        return 0;
    }

    return LINUX_ERR_EACCES;
}

int64_t TranslationLayer::HandleRmdirSystemCall(const char* Path)
{
    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    if (KernelPath[0] == '\0')
    {
        return LINUX_ERR_ENOENT;
    }

    char        EffectivePath[SYSCALL_PATH_MAX] = {};
    const char* DeletePath                      = KernelPath;

    if (KernelPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectivePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        DeletePath                             = EffectivePath;
    }

    Dentry* Existing = VFS->Lookup(DeletePath);
    if (Existing == nullptr || Existing->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    if (Existing->inode->NodeType != INODE_DIR)
    {
        return LINUX_ERR_ENOTDIR;
    }

    if (Existing->child_count != 0)
    {
        return LINUX_ERR_ENOTEMPTY;
    }

    if (!VFS->DeleteFile(DeletePath, INODE_DIR))
    {
        return LINUX_ERR_ENOENT;
    }

    return 0;
}

int64_t TranslationLayer::HandleUnlinkSystemCall(const char* Path)
{
    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    if (KernelPath[0] == '\0')
    {
        return LINUX_ERR_ENOENT;
    }

    char        EffectivePath[SYSCALL_PATH_MAX] = {};
    const char* DeletePath                      = KernelPath;

    if (KernelPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectivePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        DeletePath                             = EffectivePath;
    }

    Dentry* Existing = VFS->Lookup(DeletePath);
    if (Existing == nullptr || Existing->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    if (Existing->inode->NodeType == INODE_DIR)
    {
        return LINUX_ERR_EISDIR;
    }

    if (!VFS->DeleteFile(DeletePath, INODE_FILE))
    {
        return LINUX_ERR_ENOENT;
    }

    return 0;
}

int64_t TranslationLayer::HandleLinkSystemCall(const char* OldPath, const char* NewPath)
{
    if (Logic == nullptr || OldPath == nullptr || NewPath == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    auto ResolvePathToAbsolute = [&](const char* UserPath, char* KernelPath, uint64_t KernelPathSize, char* EffectivePath, uint64_t EffectivePathSize,
                                     const char** OutPath) -> int64_t
    {
        if (!CopyUserCString(Logic, UserPath, KernelPath, KernelPathSize))
        {
            return LINUX_ERR_EFAULT;
        }

        if (KernelPath[0] == '\0')
        {
            return LINUX_ERR_ENOENT;
        }

        *OutPath = KernelPath;
        if (KernelPath[0] == '/')
        {
            return 0;
        }

        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > EffectivePathSize)
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        *OutPath                               = EffectivePath;
        return 0;
    };

    char        KernelOldPath[SYSCALL_PATH_MAX]    = {};
    char        EffectiveOldPath[SYSCALL_PATH_MAX] = {};
    const char* LinkOldPath                        = nullptr;
    int64_t     ResolveOldResult
            = ResolvePathToAbsolute(OldPath, KernelOldPath, sizeof(KernelOldPath), EffectiveOldPath, sizeof(EffectiveOldPath), &LinkOldPath);
    if (ResolveOldResult < 0)
    {
        return ResolveOldResult;
    }

    char        KernelNewPath[SYSCALL_PATH_MAX]    = {};
    char        EffectiveNewPath[SYSCALL_PATH_MAX] = {};
    const char* LinkNewPath                        = nullptr;
    int64_t     ResolveNewResult
            = ResolvePathToAbsolute(NewPath, KernelNewPath, sizeof(KernelNewPath), EffectiveNewPath, sizeof(EffectiveNewPath), &LinkNewPath);
    if (ResolveNewResult < 0)
    {
        return ResolveNewResult;
    }

    Dentry* Source = VFS->Lookup(LinkOldPath);
    if (Source == nullptr || Source->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    if (Source->inode->NodeType == INODE_DIR)
    {
        return LINUX_ERR_EPERM;
    }

    Dentry* ExistingDestination = VFS->LookupNoFollowFinal(LinkNewPath);
    if (ExistingDestination != nullptr)
    {
        return LINUX_ERR_EEXIST;
    }

    uint64_t NewPathLength = CStrLength(LinkNewPath);
    while (NewPathLength > 1 && LinkNewPath[NewPathLength - 1] == '/')
    {
        --NewPathLength;
    }

    if (NewPathLength == 0)
    {
        return LINUX_ERR_ENOENT;
    }

    int64_t LastSlashIndex = static_cast<int64_t>(NewPathLength) - 1;
    while (LastSlashIndex >= 0 && LinkNewPath[LastSlashIndex] != '/')
    {
        --LastSlashIndex;
    }

    if (LastSlashIndex < 0)
    {
        return LINUX_ERR_ENOENT;
    }

    char ParentPath[SYSCALL_PATH_MAX] = {};
    if (LastSlashIndex == 0)
    {
        ParentPath[0] = '/';
        ParentPath[1] = '\0';
    }
    else
    {
        if (static_cast<uint64_t>(LastSlashIndex) >= sizeof(ParentPath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(ParentPath, LinkNewPath, static_cast<size_t>(LastSlashIndex));
        ParentPath[LastSlashIndex] = '\0';
    }

    Dentry* ParentDirectory = VFS->Lookup(ParentPath);
    if (ParentDirectory == nullptr || ParentDirectory->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    if (ParentDirectory->inode->NodeType != INODE_DIR)
    {
        return LINUX_ERR_ENOTDIR;
    }

    if (!VFS->LinkFile(LinkOldPath, LinkNewPath))
    {
        return LINUX_ERR_EPERM;
    }

    return 0;
}

int64_t TranslationLayer::HandleRenameSystemCall(const char* OldPath, const char* NewPath)
{
    if (Logic == nullptr || OldPath == nullptr || NewPath == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelOldPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, OldPath, KernelOldPath, sizeof(KernelOldPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelNewPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, NewPath, KernelNewPath, sizeof(KernelNewPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    if (KernelOldPath[0] == '\0' || KernelNewPath[0] == '\0')
    {
        return LINUX_ERR_ENOENT;
    }

    char        EffectiveOldPath[SYSCALL_PATH_MAX] = {};
    const char* RenameOldPath                      = KernelOldPath;
    if (KernelOldPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelOldPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectiveOldPath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectiveOldPath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectiveOldPath[Cursor++] = '/';
        }

        memcpy(EffectiveOldPath + Cursor, KernelOldPath, static_cast<size_t>(RelativeLength));
        EffectiveOldPath[Cursor + RelativeLength] = '\0';
        RenameOldPath                             = EffectiveOldPath;
    }

    char        EffectiveNewPath[SYSCALL_PATH_MAX] = {};
    const char* RenameNewPath                      = KernelNewPath;
    if (KernelNewPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelNewPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectiveNewPath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectiveNewPath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectiveNewPath[Cursor++] = '/';
        }

        memcpy(EffectiveNewPath + Cursor, KernelNewPath, static_cast<size_t>(RelativeLength));
        EffectiveNewPath[Cursor + RelativeLength] = '\0';
        RenameNewPath                             = EffectiveNewPath;
    }

    if (CStrEquals(RenameOldPath, RenameNewPath))
    {
        return 0;
    }

    Dentry* ExistingSource = VFS->Lookup(RenameOldPath);
    if (ExistingSource == nullptr || ExistingSource->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    FileType EntryType = INODE_FILE;
    if (ExistingSource->inode->NodeType == INODE_DIR)
    {
        EntryType = INODE_DIR;
    }
    else if (ExistingSource->inode->NodeType != INODE_FILE)
    {
        return LINUX_ERR_EPERM;
    }

    Dentry* ExistingDestination = VFS->Lookup(RenameNewPath);
    if (ExistingDestination != nullptr && ExistingDestination != ExistingSource)
    {
        if (EntryType == INODE_DIR)
        {
            if (ExistingDestination->inode == nullptr || ExistingDestination->inode->NodeType != INODE_DIR)
            {
                return LINUX_ERR_ENOTDIR;
            }

            if (ExistingDestination->child_count != 0)
            {
                return LINUX_ERR_ENOTEMPTY;
            }

            if (!VFS->DeleteFile(RenameNewPath, INODE_DIR))
            {
                return LINUX_ERR_EPERM;
            }
        }
        else
        {
            if (ExistingDestination->inode == nullptr)
            {
                return LINUX_ERR_ENOENT;
            }

            if (ExistingDestination->inode->NodeType == INODE_DIR)
            {
                return LINUX_ERR_EISDIR;
            }

            if (ExistingDestination->inode->NodeType != INODE_FILE)
            {
                return LINUX_ERR_EPERM;
            }

            if (!VFS->DeleteFile(RenameNewPath, INODE_FILE))
            {
                return LINUX_ERR_EPERM;
            }
        }
    }

    if (!VFS->RenameFile(RenameOldPath, RenameNewPath, EntryType))
    {
        return LINUX_ERR_EINVAL;
    }

    return 0;
}

int64_t TranslationLayer::HandleUtimesSystemCall(const char* Path, const void* Times)
{
    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Times == nullptr)
    {
        return HandleUtimensatSystemCall(LINUX_AT_FDCWD, Path, nullptr, 0);
    }

    LinuxTimeVal KernelTimes[2] = {};
    if (!Logic->CopyFromUserToKernel(Times, KernelTimes, sizeof(KernelTimes)))
    {
        return LINUX_ERR_EFAULT;
    }

    LinuxTimeSpec ConvertedTimes[2] = {};
    for (uint64_t Index = 0; Index < 2; ++Index)
    {
        if (KernelTimes[Index].Microseconds < 0 || KernelTimes[Index].Microseconds > 999999)
        {
            return LINUX_ERR_EINVAL;
        }

        ConvertedTimes[Index].Seconds     = KernelTimes[Index].Seconds;
        ConvertedTimes[Index].Nanoseconds = KernelTimes[Index].Microseconds * 1000;
    }

    return HandleUtimensatSystemCall(LINUX_AT_FDCWD, Path, ConvertedTimes, 0);
}

int64_t TranslationLayer::HandleUtimeSystemCall(const char* Path, const void* Times)
{
    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Times != nullptr)
    {
        LinuxUTimeBuf KernelTimes = {};
        if (!Logic->CopyFromUserToKernel(Times, &KernelTimes, sizeof(KernelTimes)))
        {
            return LINUX_ERR_EFAULT;
        }
    }

    return HandleUtimesSystemCall(Path, nullptr);
}

int64_t TranslationLayer::HandleFutimesatSystemCall(int64_t DirectoryFileDescriptor, const char* Path, const void* Times)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Times != nullptr)
    {
        LinuxTimeVal KernelTimes[2] = {};
        if (!Logic->CopyFromUserToKernel(Times, KernelTimes, sizeof(KernelTimes)))
        {
            return LINUX_ERR_EFAULT;
        }

        if (KernelTimes[0].Microseconds < 0 || KernelTimes[0].Microseconds > 999999)
        {
            return LINUX_ERR_EINVAL;
        }

        if (KernelTimes[1].Microseconds < 0 || KernelTimes[1].Microseconds > 999999)
        {
            return LINUX_ERR_EINVAL;
        }
    }

    Dentry* NodeDentry = nullptr;

    if (Path == nullptr)
    {
        if (DirectoryFileDescriptor < 0 || static_cast<uint64_t>(DirectoryFileDescriptor) >= MAX_OPEN_FILES_PER_PROCESS)
        {
            return LINUX_ERR_EBADF;
        }

        File* ExistingFile = CurrentProcess->FileTable[static_cast<size_t>(DirectoryFileDescriptor)];
        if (ExistingFile == nullptr || ExistingFile->Node == nullptr)
        {
            return LINUX_ERR_EBADF;
        }

        NodeDentry = ExistingFile->DirectoryEntry;
        if (NodeDentry == nullptr)
        {
            return LINUX_ERR_EFAULT;
        }
    }
    else
    {
        char KernelPath[SYSCALL_PATH_MAX] = {};
        if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
        {
            return LINUX_ERR_EFAULT;
        }

        if (KernelPath[0] == '\0')
        {
            return LINUX_ERR_ENOENT;
        }

        if (KernelPath[0] == '/')
        {
            NodeDentry = VFS->Lookup(KernelPath);
        }
        else
        {
            Dentry* BaseDirectory = nullptr;

            if (DirectoryFileDescriptor == LINUX_AT_FDCWD)
            {
                BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
            }
            else
            {
                if (DirectoryFileDescriptor < 0 || static_cast<uint64_t>(DirectoryFileDescriptor) >= MAX_OPEN_FILES_PER_PROCESS)
                {
                    return LINUX_ERR_EBADF;
                }

                File* DirectoryFile = CurrentProcess->FileTable[static_cast<size_t>(DirectoryFileDescriptor)];
                if (DirectoryFile == nullptr || DirectoryFile->Node == nullptr)
                {
                    return LINUX_ERR_EBADF;
                }

                if (DirectoryFile->Node->NodeType != INODE_DIR)
                {
                    return LINUX_ERR_ENOTDIR;
                }

                BaseDirectory = DirectoryFile->DirectoryEntry;
            }

            if (BaseDirectory == nullptr)
            {
                return LINUX_ERR_ENOENT;
            }

            NodeDentry = ResolveSimpleRelativeChildDentry(BaseDirectory, KernelPath, true);
            if (NodeDentry != nullptr)
            {
                goto finalize_futimesat_lookup;
            }

            char BasePath[SYSCALL_PATH_MAX] = {};
            if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
            {
                return LINUX_ERR_EFAULT;
            }

            char     AbsolutePath[SYSCALL_PATH_MAX] = {};
            uint64_t BasePathLength                 = CStrLength(BasePath);
            uint64_t RelativeLength                 = CStrLength(KernelPath);
            uint64_t NeedsSeparator                 = (BasePathLength > 1) ? 1 : 0;
            uint64_t RequiredBytes                  = BasePathLength + NeedsSeparator + RelativeLength + 1;
            if (RequiredBytes > sizeof(AbsolutePath))
            {
                return LINUX_ERR_EINVAL;
            }

            memcpy(AbsolutePath, BasePath, static_cast<size_t>(BasePathLength));
            uint64_t Cursor = BasePathLength;
            if (NeedsSeparator != 0)
            {
                AbsolutePath[Cursor++] = '/';
            }

            memcpy(AbsolutePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
            AbsolutePath[Cursor + RelativeLength] = '\0';

            NodeDentry = VFS->Lookup(AbsolutePath);
        }
    }

finalize_futimesat_lookup:

    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    return 0;
}

int64_t TranslationLayer::HandleUtimensatSystemCall(int64_t DirectoryFileDescriptor, const char* Path, const void* Times, int64_t Flags)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    int64_t AllowedFlags = LINUX_AT_SYMLINK_NOFOLLOW | LINUX_AT_EMPTY_PATH;
    if ((Flags & ~AllowedFlags) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Times != nullptr)
    {
        LinuxTimeSpec KernelTimes[2] = {};
        if (!Logic->CopyFromUserToKernel(Times, KernelTimes, sizeof(KernelTimes)))
        {
            return LINUX_ERR_EFAULT;
        }

        for (uint64_t Index = 0; Index < 2; ++Index)
        {
            int64_t Nanoseconds = KernelTimes[Index].Nanoseconds;
            bool    IsSpecial   = (Nanoseconds == LINUX_UTIME_NOW || Nanoseconds == LINUX_UTIME_OMIT);
            if (!IsSpecial && (Nanoseconds < 0 || Nanoseconds > 999999999))
            {
                return LINUX_ERR_EINVAL;
            }
        }
    }

    Dentry* NodeDentry = nullptr;

    bool UseEmptyPath = false;
    if (Path == nullptr)
    {
        UseEmptyPath = ((Flags & LINUX_AT_EMPTY_PATH) != 0);
        if (!UseEmptyPath)
        {
            return LINUX_ERR_EFAULT;
        }
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!UseEmptyPath)
    {
        if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
        {
            return LINUX_ERR_EFAULT;
        }

        UseEmptyPath = (KernelPath[0] == '\0' && ((Flags & LINUX_AT_EMPTY_PATH) != 0));
    }

    if (UseEmptyPath)
    {
        if (DirectoryFileDescriptor == LINUX_AT_FDCWD)
        {
            NodeDentry = CurrentProcess->CurrentFileSystemLocation;
        }
        else
        {
            if (DirectoryFileDescriptor < 0 || static_cast<uint64_t>(DirectoryFileDescriptor) >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* ExistingFile = CurrentProcess->FileTable[static_cast<size_t>(DirectoryFileDescriptor)];
            if (ExistingFile == nullptr || ExistingFile->Node == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            NodeDentry = ExistingFile->DirectoryEntry;
            if (NodeDentry == nullptr)
            {
                return LINUX_ERR_EFAULT;
            }
        }
    }
    else
    {
        if (KernelPath[0] == '\0')
        {
            return LINUX_ERR_ENOENT;
        }

        if (KernelPath[0] == '/')
        {
            NodeDentry = ((Flags & LINUX_AT_SYMLINK_NOFOLLOW) != 0) ? VFS->LookupNoFollowFinal(KernelPath) : VFS->Lookup(KernelPath);
        }
        else
        {
            Dentry* BaseDirectory = nullptr;

            if (DirectoryFileDescriptor == LINUX_AT_FDCWD)
            {
                BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
            }
            else
            {
                if (DirectoryFileDescriptor < 0 || static_cast<uint64_t>(DirectoryFileDescriptor) >= MAX_OPEN_FILES_PER_PROCESS)
                {
                    return LINUX_ERR_EBADF;
                }

                File* DirectoryFile = CurrentProcess->FileTable[static_cast<size_t>(DirectoryFileDescriptor)];
                if (DirectoryFile == nullptr || DirectoryFile->Node == nullptr)
                {
                    return LINUX_ERR_EBADF;
                }

                if (DirectoryFile->Node->NodeType != INODE_DIR)
                {
                    return LINUX_ERR_ENOTDIR;
                }

                BaseDirectory = DirectoryFile->DirectoryEntry;
            }

            if (BaseDirectory == nullptr)
            {
                return LINUX_ERR_ENOENT;
            }

            bool FollowFinalSymlink = ((Flags & LINUX_AT_SYMLINK_NOFOLLOW) == 0);
            NodeDentry              = ResolveSimpleRelativeChildDentry(BaseDirectory, KernelPath, FollowFinalSymlink);
            if (NodeDentry != nullptr)
            {
                goto finalize_utimensat_lookup;
            }

            char BasePath[SYSCALL_PATH_MAX] = {};
            if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
            {
                return LINUX_ERR_EFAULT;
            }

            char     AbsolutePath[SYSCALL_PATH_MAX] = {};
            uint64_t BasePathLength                 = CStrLength(BasePath);
            uint64_t RelativeLength                 = CStrLength(KernelPath);
            uint64_t NeedsSeparator                 = (BasePathLength > 1) ? 1 : 0;
            uint64_t RequiredBytes                  = BasePathLength + NeedsSeparator + RelativeLength + 1;
            if (RequiredBytes > sizeof(AbsolutePath))
            {
                return LINUX_ERR_EINVAL;
            }

            memcpy(AbsolutePath, BasePath, static_cast<size_t>(BasePathLength));
            uint64_t Cursor = BasePathLength;
            if (NeedsSeparator != 0)
            {
                AbsolutePath[Cursor++] = '/';
            }

            memcpy(AbsolutePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
            AbsolutePath[Cursor + RelativeLength] = '\0';

            NodeDentry = ((Flags & LINUX_AT_SYMLINK_NOFOLLOW) != 0) ? VFS->LookupNoFollowFinal(AbsolutePath) : VFS->Lookup(AbsolutePath);
        }
    }

finalize_utimensat_lookup:

    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    return 0;
}

int64_t TranslationLayer::HandleChrootSystemCall(const char* Path)
{
    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    if (VFS == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    if (KernelPath[0] == '\0')
    {
        return LINUX_ERR_ENOENT;
    }

    Dentry* RootDentry = VFS->Lookup(KernelPath);
    if (RootDentry == nullptr || RootDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    if (RootDentry->inode->NodeType != INODE_DIR)
    {
        return LINUX_ERR_ENOTDIR;
    }

    if (!VFS->SetRoot(RootDentry))
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    CurrentProcess->CurrentFileSystemLocation = RootDentry;
    SyncVforkParentFileSystemLocation(PM, CurrentProcess);

    return 0;
}

int64_t TranslationLayer::HandleMountSystemCall(const char* Source, const char* Target, const char* FileSystemType, uint64_t MountFlags, const void* Data)
{
    (void) MountFlags;
    (void) Data;

    if (Logic == nullptr || Source == nullptr || Target == nullptr || FileSystemType == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    constexpr uint64_t SYSCALL_MOUNT_SOURCE_MAX = 256;
    constexpr uint64_t SYSCALL_MOUNT_TARGET_MAX = 1024;

    char KernelSourcePath[SYSCALL_MOUNT_SOURCE_MAX] = {};
    if (!CopyUserCString(Logic, Source, KernelSourcePath, sizeof(KernelSourcePath)))
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelTargetPath[SYSCALL_MOUNT_TARGET_MAX] = {};
    if (!CopyUserCString(Logic, Target, KernelTargetPath, sizeof(KernelTargetPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelFileSystemType[64] = {};
    if (!CopyUserCString(Logic, FileSystemType, KernelFileSystemType, sizeof(KernelFileSystemType)))
    {
        return LINUX_ERR_EFAULT;
    }

    if (KernelSourcePath[0] == '\0' || KernelTargetPath[0] == '\0' || KernelFileSystemType[0] == '\0')
    {
        return LINUX_ERR_EINVAL;
    }

    if (!IsSupportedMountFileSystemType(KernelFileSystemType))
    {
        return LINUX_ERR_ENODEV;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    const char* MountLocation = KernelTargetPath;

    if (KernelTargetPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_MOUNT_TARGET_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        char AbsoluteMountPath[SYSCALL_MOUNT_TARGET_MAX] = {};

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelTargetPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(AbsoluteMountPath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(AbsoluteMountPath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            AbsoluteMountPath[Cursor++] = '/';
        }

        memcpy(AbsoluteMountPath + Cursor, KernelTargetPath, static_cast<size_t>(RelativeLength));
        AbsoluteMountPath[Cursor + RelativeLength] = '\0';
        return Logic->InitializeExtendedFileSystem(KernelSourcePath, AbsoluteMountPath) ? 0 : LINUX_ERR_ENODEV;
    }

    return Logic->InitializeExtendedFileSystem(KernelSourcePath, MountLocation) ? 0 : LINUX_ERR_ENODEV;
}

int64_t TranslationLayer::HandleMprotectSystemCall(void* Address, uint64_t Length, int64_t Protection)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Length == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    constexpr int64_t LINUX_MPROTECT_ALLOWED_MASK = (LINUX_PROT_READ | LINUX_PROT_WRITE | LINUX_PROT_EXEC);
    if ((Protection & ~LINUX_MPROTECT_ALLOWED_MASK) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t StartAddress = reinterpret_cast<uint64_t>(Address);
    if ((StartAddress & (PAGE_SIZE - 1)) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t EndAddress = StartAddress + Length;
    if (EndAddress < StartAddress)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t ProtectedLength = AlignUpToPageBoundary(Length);
    if (ProtectedLength == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER || CurrentProcess->AddressSpace == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr || ActiveDispatcher->GetResourceLayer() == nullptr || ActiveDispatcher->GetResourceLayer()->GetPMM() == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    uint64_t UserPageTable = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
    if (UserPageTable == 0)
    {
        return LINUX_ERR_EFAULT;
    }

    bool UserAccess = (Protection != LINUX_PROT_NONE);
    bool Writeable  = (Protection & LINUX_PROT_WRITE) != 0;
    bool Executable = (Protection & LINUX_PROT_EXEC) != 0;

    VirtualMemoryManager UserVMM(UserPageTable, *ActiveDispatcher->GetResourceLayer()->GetPMM());

    uint64_t ProtectedPages = ProtectedLength / PAGE_SIZE;
    for (uint64_t PageIndex = 0; PageIndex < ProtectedPages; ++PageIndex)
    {
        uint64_t VirtualPage = StartAddress + (PageIndex * PAGE_SIZE);
        if (!UserVMM.ProtectPage(VirtualPage, UserAccess, Writeable, Executable))
        {
            return LINUX_ERR_ENOMEM;
        }
    }

    return 0;
}

int64_t TranslationLayer::HandleBrkSystemCall(uint64_t Address)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER || CurrentProcess->AddressSpace == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr || ActiveDispatcher->GetResourceLayer() == nullptr || ActiveDispatcher->GetResourceLayer()->GetPMM() == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ResourceLayer*         Resource = ActiveDispatcher->GetResourceLayer();
    PhysicalMemoryManager* PMM      = Resource->GetPMM();

    VirtualAddressSpace* AddressSpace = CurrentProcess->AddressSpace;

    uint64_t HeapStart = AddressSpace->GetHeapVirtualAddressStart();
    uint64_t HeapSize  = AddressSpace->GetHeapSize();
    uint64_t HeapEnd   = HeapStart + HeapSize;

    if (HeapEnd < HeapStart)
    {
        return LINUX_ERR_EFAULT;
    }

    if (CurrentProcess->ProgramBreak == 0)
    {
        CurrentProcess->ProgramBreak = HeapStart;
    }

    if (Address == 0)
    {
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    if (Address < HeapStart)
    {
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    uint64_t StackStart = AddressSpace->GetStackVirtualAddressStart();
    if (Address >= StackStart)
    {
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    if (Address <= HeapEnd)
    {
        CurrentProcess->ProgramBreak = Address;
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    uint64_t RequestedHeapSize = Address - HeapStart;
    uint64_t NewHeapSize       = AlignUpToPageBoundary(RequestedHeapSize);
    if (NewHeapSize == 0 || NewHeapSize < HeapSize)
    {
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    uint64_t NewHeapEnd = HeapStart + NewHeapSize;
    if (NewHeapEnd < HeapStart || NewHeapEnd > StackStart)
    {
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    uint64_t NewHeapPages    = NewHeapSize / PAGE_SIZE;
    void*    NewHeapPhysical = PMM->AllocatePagesFromDescriptor(NewHeapPages);
    if (NewHeapPhysical == nullptr)
    {
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    kmemset(NewHeapPhysical, 0, static_cast<size_t>(NewHeapSize));

    uint64_t OldHeapPhysical = AddressSpace->GetHeapPhysicalAddress();
    if (OldHeapPhysical != 0 && HeapSize != 0)
    {
        memcpy(NewHeapPhysical, reinterpret_cast<const void*>(OldHeapPhysical), static_cast<size_t>(HeapSize));
    }

    uint64_t UserPageTable = AddressSpace->GetPageMapL4TableAddr();
    if (UserPageTable == 0)
    {
        PMM->FreePagesFromDescriptor(NewHeapPhysical, NewHeapPages);
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    uint64_t OldHeapPages = (HeapSize + PAGE_SIZE - 1) / PAGE_SIZE;

    VirtualMemoryManager UserVMM(UserPageTable, *PMM);
    uint64_t             MappedPages = 0;
    for (uint64_t PageIndex = 0; PageIndex < NewHeapPages; ++PageIndex)
    {
        uint64_t PhysicalPage = reinterpret_cast<uint64_t>(NewHeapPhysical) + (PageIndex * PAGE_SIZE);
        uint64_t VirtualPage  = HeapStart + (PageIndex * PAGE_SIZE);
        if (!UserVMM.MapPage(PhysicalPage, VirtualPage, PageMappingFlags(true, true)))
        {
            for (uint64_t RollbackIndex = 0; RollbackIndex < MappedPages; ++RollbackIndex)
            {
                uint64_t RollbackVirtualPage = HeapStart + (RollbackIndex * PAGE_SIZE);
                if (RollbackIndex < OldHeapPages && OldHeapPhysical != 0)
                {
                    uint64_t OldPhysicalPage = OldHeapPhysical + (RollbackIndex * PAGE_SIZE);
                    UserVMM.MapPage(OldPhysicalPage, RollbackVirtualPage, PageMappingFlags(true, true));
                }
                else
                {
                    UserVMM.UnmapPage(RollbackVirtualPage);
                }
            }

            PMM->FreePagesFromDescriptor(NewHeapPhysical, NewHeapPages);
            return static_cast<int64_t>(CurrentProcess->ProgramBreak);
        }

        ++MappedPages;
    }

    if (OldHeapPhysical != 0 && HeapSize != 0)
    {
        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(OldHeapPhysical), OldHeapPages);
    }

    AddressSpace->SetHeapPhysicalAddress(reinterpret_cast<uint64_t>(NewHeapPhysical));
    AddressSpace->SetHeapSize(NewHeapSize);

    uint64_t ActivePageTable = Resource->ReadCurrentPageTable();
    if (ActivePageTable == UserPageTable)
    {
        Resource->LoadPageTable(ActivePageTable);
    }

    CurrentProcess->ProgramBreak = Address;
    return static_cast<int64_t>(CurrentProcess->ProgramBreak);
}

int64_t TranslationLayer::HandleGetpidSystemCall()
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    return static_cast<int64_t>(CurrentProcess->Id);
}

int64_t TranslationLayer::HandleGetppidSystemCall()
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (CurrentProcess->ParrentId == PROCESS_ID_INVALID)
    {
        return 0;
    }

    return static_cast<int64_t>(CurrentProcess->ParrentId);
}

int64_t TranslationLayer::HandleKillSystemCall(int64_t Pid, int64_t Signal)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Signal < 0 || Signal > LINUX_SIGNAL_MAX)
    {
        return LINUX_ERR_EINVAL;
    }

    bool ProbeOnly           = (Signal == 0);
    bool AnyMatchingTarget   = false;
    bool AnySignalDelivered  = false;
    bool AnyPermissionDenied = false;

    auto TrySignalTarget = [&](Process* TargetProcess)
    {
        if (TargetProcess == nullptr || TargetProcess->Status == PROCESS_TERMINATED)
        {
            return;
        }

        AnyMatchingTarget = true;

        if (TargetProcess->Level != PROCESS_LEVEL_USER)
        {
            AnyPermissionDenied = true;
            return;
        }

        if (ProbeOnly)
        {
            AnySignalDelivered = true;
            return;
        }

        if (Logic->SignalProcess(TargetProcess->Id, Signal))
        {
            AnySignalDelivered = true;
        }
    };

    auto GetEffectiveProcessGroupId = [](const Process* ProcessEntry) -> int32_t
    {
        if (ProcessEntry == nullptr)
        {
            return -1;
        }

        return (ProcessEntry->ProcessGroupId > 0) ? ProcessEntry->ProcessGroupId : static_cast<int32_t>(ProcessEntry->Id);
    };

    if (Pid > 0)
    {
        Process* TargetProcess = nullptr;
        if (Pid <= 0xFF)
        {
            TargetProcess = PM->GetProcessById(static_cast<uint8_t>(Pid));
        }

        TrySignalTarget(TargetProcess);
    }
    else if (Pid == 0)
    {
        int32_t CallerProcessGroupId = GetEffectiveProcessGroupId(CurrentProcess);
        for (size_t Index = 0; Index < PM->GetMaxProcesses(); ++Index)
        {
            Process* CandidateProcess = PM->GetProcessById(static_cast<uint8_t>(Index));
            if (CandidateProcess == nullptr || CandidateProcess->Status == PROCESS_TERMINATED)
            {
                continue;
            }

            if (GetEffectiveProcessGroupId(CandidateProcess) != CallerProcessGroupId)
            {
                continue;
            }

            TrySignalTarget(CandidateProcess);
        }
    }
    else if (Pid == -1)
    {
        for (size_t Index = 0; Index < PM->GetMaxProcesses(); ++Index)
        {
            Process* CandidateProcess = PM->GetProcessById(static_cast<uint8_t>(Index));
            if (CandidateProcess == nullptr || CandidateProcess->Status == PROCESS_TERMINATED)
            {
                continue;
            }

            if (CandidateProcess->Id == 1 || CandidateProcess->Id == CurrentProcess->Id)
            {
                continue;
            }

            TrySignalTarget(CandidateProcess);
        }
    }
    else
    {
        int64_t TargetProcessGroupId = -Pid;
        if (TargetProcessGroupId <= 0)
        {
            return LINUX_ERR_EINVAL;
        }

        for (size_t Index = 0; Index < PM->GetMaxProcesses(); ++Index)
        {
            Process* CandidateProcess = PM->GetProcessById(static_cast<uint8_t>(Index));
            if (CandidateProcess == nullptr || CandidateProcess->Status == PROCESS_TERMINATED)
            {
                continue;
            }

            if (GetEffectiveProcessGroupId(CandidateProcess) != static_cast<int32_t>(TargetProcessGroupId))
            {
                continue;
            }

            TrySignalTarget(CandidateProcess);
        }
    }

    if (AnySignalDelivered)
    {
        return 0;
    }

    if (AnyPermissionDenied && AnyMatchingTarget)
    {
        return LINUX_ERR_EPERM;
    }

    return LINUX_ERR_ESRCH;
}

int64_t TranslationLayer::HandleSetpgidSystemCall(int64_t Pid, int64_t ProcessGroupId)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    int64_t EffectivePid = (Pid == 0) ? static_cast<int64_t>(CurrentProcess->Id) : Pid;
    if (EffectivePid <= 0 || EffectivePid > 0xFF)
    {
        return LINUX_ERR_EINVAL;
    }

    Process* TargetProcess = PM->GetProcessById(static_cast<uint8_t>(EffectivePid));
    if (TargetProcess == nullptr || TargetProcess->Status == PROCESS_TERMINATED)
    {
        return LINUX_ERR_ESRCH;
    }

    if (TargetProcess->Id != CurrentProcess->Id)
    {
        return LINUX_ERR_EPERM;
    }

    int64_t EffectiveProcessGroupId = (ProcessGroupId == 0) ? EffectivePid : ProcessGroupId;
    if (EffectiveProcessGroupId <= 0)
    {
        return LINUX_ERR_EINVAL;
    }

    TargetProcess->ProcessGroupId = static_cast<int32_t>(EffectiveProcessGroupId);
    if (TargetProcess->SessionId <= 0)
    {
        TargetProcess->SessionId = static_cast<int32_t>(CurrentProcess->Id);
    }

    return 0;
}

int64_t TranslationLayer::HandleGetprioritySystemCall(int64_t Which, int64_t Who)
{
    (void) Which;
    (void) Who;

    constexpr int64_t LINUX_DEFAULT_KERNEL_PRIORITY = 20;
    return LINUX_DEFAULT_KERNEL_PRIORITY;
}

int64_t TranslationLayer::HandleSetprioritySystemCall(int64_t Which, int64_t Who, int64_t NiceValue)
{
    (void) Which;
    (void) Who;
    (void) NiceValue;

    // Scheduler priority/nice control is currently unsupported;
    return 0;
}

int64_t TranslationLayer::HandleUmaskSystemCall(uint64_t Mask)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    uint32_t OldMask = (CurrentProcess->FileCreationMask & LINUX_UMASK_PERMISSION_BITS);
    CurrentProcess->FileCreationMask = static_cast<uint32_t>(Mask & LINUX_UMASK_PERMISSION_BITS);
    return static_cast<int64_t>(OldMask);
}

int64_t TranslationLayer::HandleGetrlimitSystemCall(int64_t Resource, void* Limit)
{
    if (Logic == nullptr || Limit == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (!IsSupportedLinuxRlimitResource(Resource))
    {
        return LINUX_ERR_EINVAL;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    EnsureProcessLinuxResourceLimitsInitialized(CurrentProcess);

    LinuxRLimit64 KernelLimit = {
        CurrentProcess->ResourceLimitCurrent[Resource],
        CurrentProcess->ResourceLimitMaximum[Resource],
    };

    return Logic->CopyFromKernelToUser(&KernelLimit, Limit, sizeof(KernelLimit)) ? 0 : LINUX_ERR_EFAULT;
}

int64_t TranslationLayer::HandlePrlimit64SystemCall(int64_t Pid, int64_t Resource, const void* NewLimit, void* OldLimit)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (!IsSupportedLinuxRlimitResource(Resource))
    {
        return LINUX_ERR_EINVAL;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* TargetProcess = nullptr;
    if (Pid == 0)
    {
        TargetProcess = CurrentProcess;
    }
    else
    {
        if (Pid < 0 || Pid > 0xFF)
        {
            return LINUX_ERR_EINVAL;
        }

        TargetProcess = PM->GetProcessById(static_cast<uint8_t>(Pid));
        if (TargetProcess == nullptr || TargetProcess->Status == PROCESS_TERMINATED)
        {
            return LINUX_ERR_ESRCH;
        }

        if (TargetProcess->Id != CurrentProcess->Id)
        {
            return LINUX_ERR_EPERM;
        }
    }

    EnsureProcessLinuxResourceLimitsInitialized(TargetProcess);

    if (OldLimit != nullptr)
    {
        LinuxRLimit64 KernelOldLimit = {
            TargetProcess->ResourceLimitCurrent[Resource],
            TargetProcess->ResourceLimitMaximum[Resource],
        };

        if (!Logic->CopyFromKernelToUser(&KernelOldLimit, OldLimit, sizeof(KernelOldLimit)))
        {
            return LINUX_ERR_EFAULT;
        }
    }

    if (NewLimit != nullptr)
    {
        LinuxRLimit64 KernelNewLimit = {};
        if (!Logic->CopyFromUserToKernel(NewLimit, &KernelNewLimit, sizeof(KernelNewLimit)))
        {
            return LINUX_ERR_EFAULT;
        }

        if (KernelNewLimit.Current > KernelNewLimit.Maximum)
        {
            return LINUX_ERR_EINVAL;
        }

        TargetProcess->ResourceLimitCurrent[Resource] = KernelNewLimit.Current;
        TargetProcess->ResourceLimitMaximum[Resource] = KernelNewLimit.Maximum;
    }

    return 0;
}

int64_t TranslationLayer::HandleGetpgrpSystemCall()
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (CurrentProcess->ProcessGroupId <= 0)
    {
        CurrentProcess->ProcessGroupId = static_cast<int32_t>(CurrentProcess->Id);
    }

    return static_cast<int64_t>(CurrentProcess->ProcessGroupId);
}

int64_t TranslationLayer::HandleSetsidSystemCall()
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    int64_t CurrentProcessId = static_cast<int64_t>(CurrentProcess->Id);
    if (CurrentProcess->ProcessGroupId == CurrentProcessId)
    {
        return LINUX_ERR_EPERM;
    }

    CurrentProcess->SessionId      = static_cast<int32_t>(CurrentProcessId);
    CurrentProcess->ProcessGroupId = static_cast<int32_t>(CurrentProcessId);
    return CurrentProcessId;
}

int64_t TranslationLayer::HandleGetpgidSystemCall(int64_t Pid)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    int64_t EffectivePid = (Pid == 0) ? static_cast<int64_t>(CurrentProcess->Id) : Pid;
    if (EffectivePid <= 0 || EffectivePid > 0xFF)
    {
        return LINUX_ERR_EINVAL;
    }

    Process* TargetProcess = PM->GetProcessById(static_cast<uint8_t>(EffectivePid));
    if (TargetProcess == nullptr || TargetProcess->Status == PROCESS_TERMINATED)
    {
        return LINUX_ERR_ESRCH;
    }

    if (TargetProcess->ProcessGroupId <= 0)
    {
        TargetProcess->ProcessGroupId = static_cast<int32_t>(TargetProcess->Id);
    }

    return static_cast<int64_t>(TargetProcess->ProcessGroupId);
}

int64_t TranslationLayer::HandleGetsidSystemCall(int64_t Pid)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    int64_t EffectivePid = (Pid == 0) ? static_cast<int64_t>(CurrentProcess->Id) : Pid;
    if (EffectivePid <= 0 || EffectivePid > 0xFF)
    {
        return LINUX_ERR_EINVAL;
    }

    Process* TargetProcess = PM->GetProcessById(static_cast<uint8_t>(EffectivePid));
    if (TargetProcess == nullptr || TargetProcess->Status == PROCESS_TERMINATED)
    {
        return LINUX_ERR_ESRCH;
    }

    if (TargetProcess->SessionId <= 0)
    {
        TargetProcess->SessionId = static_cast<int32_t>(TargetProcess->Id);
    }

    return static_cast<int64_t>(TargetProcess->SessionId);
}

int64_t TranslationLayer::HandleGetuidSystemCall()
{
    return 0;
}

int64_t TranslationLayer::HandleUnameSystemCall(void* Buffer)
{
    if (Logic == nullptr || Buffer == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    LinuxUtsName Uts = {};
    strcpy(Uts.SysName, "TwistedOS");
    strcpy(Uts.NodeName, "twistedos");
    strcpy(Uts.Release, "0.1.0");
    strcpy(Uts.Version, "TwistedOS");
    strcpy(Uts.Machine, "x86_64");
    Uts.DomainName[0] = '\0';

    return Logic->CopyFromKernelToUser(&Uts, Buffer, sizeof(Uts)) ? 0 : LINUX_ERR_EFAULT;
}

int64_t TranslationLayer::HandleGetgidSystemCall()
{
    return 0;
}

int64_t TranslationLayer::HandleGeteuidSystemCall()
{
    return 0;
}

int64_t TranslationLayer::HandleGetegidSystemCall()
{
    return 0;
}

int64_t TranslationLayer::HandleDup2SystemCall(uint64_t OldFileDescriptor, uint64_t NewFileDescriptor)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (OldFileDescriptor >= MAX_OPEN_FILES_PER_PROCESS || NewFileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* SourceFile = CurrentProcess->FileTable[OldFileDescriptor];
    if (SourceFile == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (OldFileDescriptor == NewFileDescriptor)
    {
        return static_cast<int64_t>(NewFileDescriptor);
    }

    File* ExistingTargetFile = CurrentProcess->FileTable[NewFileDescriptor];
    if (ExistingTargetFile != nullptr)
    {
        delete ExistingTargetFile;
        CurrentProcess->FileTable[NewFileDescriptor] = nullptr;
    }

    File* DuplicatedFile = new File;
    if (DuplicatedFile == nullptr)
    {
        return LINUX_ERR_ENOMEM;
    }

    *DuplicatedFile                              = *SourceFile;
    DuplicatedFile->FileDescriptor               = NewFileDescriptor;
    DuplicatedFile->DescriptorFlags              = 0;
    CurrentProcess->FileTable[NewFileDescriptor] = DuplicatedFile;

    return static_cast<int64_t>(NewFileDescriptor);
}

int64_t TranslationLayer::HandlePauseSystemCall()
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER)
    {
        return LINUX_ERR_EINVAL;
    }

    Logic->BlockProcess(CurrentProcess->Id);
    return LINUX_ERR_EINTR;
}

int64_t TranslationLayer::HandleNanosleepSystemCall(const void* RequestedTime, void* RemainingTime)
{
    if (Logic == nullptr || RequestedTime == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER)
    {
        return LINUX_ERR_EINVAL;
    }

    LinuxTimeSpec RequestedKernelTime = {};
    if (!Logic->CopyFromUserToKernel(RequestedTime, &RequestedKernelTime, sizeof(RequestedKernelTime)))
    {
        return LINUX_ERR_EFAULT;
    }

    if (RequestedKernelTime.Seconds < 0)
    {
        return LINUX_ERR_EINVAL;
    }

    if (RequestedKernelTime.Nanoseconds < 0 || RequestedKernelTime.Nanoseconds > 999999999)
    {
        return LINUX_ERR_EINVAL;
    }

    constexpr uint64_t NANOSECONDS_PER_TICK  = 10000000;
    uint64_t           SleepTicksFromSeconds = static_cast<uint64_t>(RequestedKernelTime.Seconds) * 100;

    if (RequestedKernelTime.Seconds > 0 && (SleepTicksFromSeconds / 100) != static_cast<uint64_t>(RequestedKernelTime.Seconds))
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t RequestedNanoseconds      = static_cast<uint64_t>(RequestedKernelTime.Nanoseconds);
    uint64_t SleepTicksFromNanoseconds = (RequestedNanoseconds + (NANOSECONDS_PER_TICK - 1)) / NANOSECONDS_PER_TICK;

    uint64_t SleepTicks = SleepTicksFromSeconds + SleepTicksFromNanoseconds;
    if (SleepTicks < SleepTicksFromSeconds)
    {
        return LINUX_ERR_EINVAL;
    }

    if (SleepTicks != 0)
    {
        Logic->SleepProcess(CurrentProcess->Id, SleepTicks);
    }

    if (RemainingTime != nullptr)
    {
        LinuxTimeSpec RemainingKernelTime = {};
        if (!Logic->CopyFromKernelToUser(&RemainingKernelTime, RemainingTime, sizeof(RemainingKernelTime)))
        {
            return LINUX_ERR_EFAULT;
        }
    }

    return 0;
}

int64_t TranslationLayer::HandleSetitimerSystemCall(int64_t Which, const void* NewValue, void* OldValue)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Which != LINUX_ITIMER_REAL && Which != LINUX_ITIMER_VIRTUAL && Which != LINUX_ITIMER_PROF)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Which != LINUX_ITIMER_REAL)
    {
        return LINUX_ERR_ENOSYS;
    }

    auto ConvertTicksToTimeVal = [](uint64_t Ticks) -> LinuxTimeVal
    {
        LinuxTimeVal Result = {};

        uint64_t TotalMicroseconds = Ticks * LINUX_TIMER_MICROSECONDS_PER_TICK;
        Result.Seconds      = static_cast<int64_t>(TotalMicroseconds / 1000000ULL);
        Result.Microseconds = static_cast<int64_t>(TotalMicroseconds % 1000000ULL);

        return Result;
    };

    if (OldValue != nullptr)
    {
        LinuxITimerVal OldKernelValue = {};
        OldKernelValue.Interval = ConvertTicksToTimeVal(CurrentProcess->RealIntervalTimerIntervalTicks);
        OldKernelValue.Value    = ConvertTicksToTimeVal(CurrentProcess->RealIntervalTimerRemainingTicks);

        if (!Logic->CopyFromKernelToUser(&OldKernelValue, OldValue, sizeof(OldKernelValue)))
        {
            return LINUX_ERR_EFAULT;
        }
    }

    if (NewValue == nullptr)
    {
        return 0;
    }

    LinuxITimerVal NewKernelValue = {};
    if (!Logic->CopyFromUserToKernel(NewValue, &NewKernelValue, sizeof(NewKernelValue)))
    {
        return LINUX_ERR_EFAULT;
    }

    auto IsValidTimeVal = [](const LinuxTimeVal& Value) -> bool
    {
        if (Value.Seconds < 0)
        {
            return false;
        }

        if (Value.Microseconds < 0 || Value.Microseconds >= 1000000)
        {
            return false;
        }

        return true;
    };

    if (!IsValidTimeVal(NewKernelValue.Interval) || !IsValidTimeVal(NewKernelValue.Value))
    {
        return LINUX_ERR_EINVAL;
    }

    auto ConvertTimeValToTicks = [](const LinuxTimeVal& Value, bool RoundUpSubTick) -> uint64_t
    {
        uint64_t SecondsPart = static_cast<uint64_t>(Value.Seconds);
        uint64_t MicroPart   = static_cast<uint64_t>(Value.Microseconds);

        uint64_t TicksFromSeconds = SecondsPart * LINUX_TIMER_TICKS_PER_SECOND;
        uint64_t TicksFromMicroseconds = MicroPart / LINUX_TIMER_MICROSECONDS_PER_TICK;
        uint64_t RemainderMicroseconds = MicroPart % LINUX_TIMER_MICROSECONDS_PER_TICK;

        uint64_t ResultTicks = TicksFromSeconds + TicksFromMicroseconds;
        if (RoundUpSubTick && ResultTicks == 0 && (SecondsPart != 0 || MicroPart != 0 || RemainderMicroseconds != 0))
        {
            ResultTicks = 1;
        }

        return ResultTicks;
    };

    uint64_t NewIntervalTicks = ConvertTimeValToTicks(NewKernelValue.Interval, false);
    uint64_t NewValueTicks    = ConvertTimeValToTicks(NewKernelValue.Value, true);

    CurrentProcess->RealIntervalTimerIntervalTicks  = NewIntervalTicks;
    CurrentProcess->RealIntervalTimerRemainingTicks = NewValueTicks;

    return 0;
}

int64_t TranslationLayer::HandleGettimeofdaySystemCall(void* TimeValue, void* TimeZone)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    LinuxTimeVal KernelTimeValue = {};

    if (TimeValue != nullptr)
    {
        if (!Logic->CopyFromKernelToUser(&KernelTimeValue, TimeValue, sizeof(KernelTimeValue)))
        {
            return LINUX_ERR_EFAULT;
        }
    }

    if (TimeZone != nullptr)
    {
        uint64_t ZeroTimeZone = 0;
        if (!Logic->CopyFromKernelToUser(&ZeroTimeZone, TimeZone, sizeof(ZeroTimeZone)))
        {
            return LINUX_ERR_EFAULT;
        }
    }

    return 0;
}

int64_t TranslationLayer::HandleClockGettimeSystemCall(int64_t ClockId, void* TimeSpec)
{
    if (Logic == nullptr || TimeSpec == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (!IsSupportedLinuxClockId(ClockId))
    {
        return LINUX_ERR_EINVAL;
    }

    LinuxTimeSpec KernelTimeSpec = {};
    return Logic->CopyFromKernelToUser(&KernelTimeSpec, TimeSpec, sizeof(KernelTimeSpec)) ? 0 : LINUX_ERR_EFAULT;
}

int64_t TranslationLayer::HandleClockGetresSystemCall(int64_t ClockId, void* TimeSpec)
{
    if (!IsSupportedLinuxClockId(ClockId))
    {
        return LINUX_ERR_EINVAL;
    }

    if (TimeSpec == nullptr)
    {
        return 0;
    }

    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    LinuxTimeSpec KernelTimeSpec = {
        0,
        static_cast<int64_t>(LINUX_TIMER_NANOSECONDS_PER_TICK),
    };

    return Logic->CopyFromKernelToUser(&KernelTimeSpec, TimeSpec, sizeof(KernelTimeSpec)) ? 0 : LINUX_ERR_EFAULT;
}

int64_t TranslationLayer::HandleForkSystemCall()
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    CurrentProcess->UserFSBase = GetUserFSBase();

    uint8_t ChildId = Logic->CopyProcess(CurrentProcess->Id);
    if (ChildId == PROCESS_ID_INVALID)
    {
        return LINUX_ERR_EAGAIN;
    }

    Process* ChildProcess = PM->GetProcessById(ChildId);
    if (ChildProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (!CurrentProcess->HasSavedSystemCallFrame)
    {
        return LINUX_ERR_EFAULT;
    }

    const ProcessSavedSystemCallFrame& SavedFrame = CurrentProcess->SavedSystemCallFrame;

    ChildProcess->State.rax    = SavedFrame.UserRAX;
    ChildProcess->State.rcx    = 0;
    ChildProcess->State.rdx    = SavedFrame.UserRDX;
    ChildProcess->State.rbx    = SavedFrame.UserRBX;
    ChildProcess->State.rbp    = SavedFrame.UserRBP;
    ChildProcess->State.rsi    = SavedFrame.UserRSI;
    ChildProcess->State.rdi    = SavedFrame.UserRDI;
    ChildProcess->State.r8     = SavedFrame.UserR8;
    ChildProcess->State.r9     = SavedFrame.UserR9;
    ChildProcess->State.r10    = SavedFrame.UserR10;
    ChildProcess->State.r11    = 0;
    ChildProcess->State.r12    = SavedFrame.UserR12;
    ChildProcess->State.r13    = SavedFrame.UserR13;
    ChildProcess->State.r14    = SavedFrame.UserR14;
    ChildProcess->State.r15    = SavedFrame.UserR15;
    ChildProcess->State.rip    = SavedFrame.UserRIP;
    ChildProcess->State.rflags = SavedFrame.UserRFLAGS;
    ChildProcess->State.rsp    = SavedFrame.UserRSP;
    ChildProcess->State.cs     = USER_CS;
    ChildProcess->State.ss     = USER_SS;
    ChildProcess->State.rax    = 0;

    return static_cast<int64_t>(ChildId);
}

int64_t TranslationLayer::HandleVforkSystemCall()
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* ParentProcess = PM->GetRunningProcess();
    if (ParentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (ParentProcess->WaitingForVforkChild)
    {
        return LINUX_ERR_EAGAIN;
    }

    if (!ParentProcess->HasSavedSystemCallFrame || ParentProcess->AddressSpace == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ParentProcess->UserFSBase = GetUserFSBase();

    const ProcessSavedSystemCallFrame& SavedFrame = ParentProcess->SavedSystemCallFrame;

    CpuState ChildState = {};
    ChildState.rax      = 0;
    ChildState.rcx      = 0;
    ChildState.rdx      = SavedFrame.UserRDX;
    ChildState.rbx      = SavedFrame.UserRBX;
    ChildState.rbp      = SavedFrame.UserRBP;
    ChildState.rsi      = SavedFrame.UserRSI;
    ChildState.rdi      = SavedFrame.UserRDI;
    ChildState.r8       = SavedFrame.UserR8;
    ChildState.r9       = SavedFrame.UserR9;
    ChildState.r10      = SavedFrame.UserR10;
    ChildState.r11      = 0;
    ChildState.r12      = SavedFrame.UserR12;
    ChildState.r13      = SavedFrame.UserR13;
    ChildState.r14      = SavedFrame.UserR14;
    ChildState.r15      = SavedFrame.UserR15;
    ChildState.rip      = SavedFrame.UserRIP;
    ChildState.rflags   = SavedFrame.UserRFLAGS;
    ChildState.rsp      = SavedFrame.UserRSP;
    ChildState.cs       = USER_CS;
    ChildState.ss       = USER_SS;

    uint8_t ChildId = PM->CreateUserProcess(reinterpret_cast<void*>(ParentProcess->AddressSpace->GetStackVirtualAddressStart()), ChildState, ParentProcess->AddressSpace, ParentProcess->FileType);
    if (ChildId == PROCESS_ID_INVALID)
    {
        return LINUX_ERR_EAGAIN;
    }

    Process* ChildProcess = PM->GetProcessById(ChildId);
    if (ChildProcess == nullptr)
    {
        PM->KillProcess(ChildId);
        return LINUX_ERR_EFAULT;
    }

    ChildProcess->ParrentId                 = ParentProcess->Id;
    ChildProcess->UserFSBase                = ParentProcess->UserFSBase;
    ChildProcess->ProcessGroupId            = ParentProcess->ProcessGroupId;
    ChildProcess->SessionId                 = ParentProcess->SessionId;
    ChildProcess->BlockedSignalMask         = ParentProcess->BlockedSignalMask;
    ChildProcess->ClearChildTidAddress      = ParentProcess->ClearChildTidAddress;
    ChildProcess->ProgramBreak              = ParentProcess->ProgramBreak;
    ChildProcess->CurrentFileSystemLocation = ParentProcess->CurrentFileSystemLocation;

    for (size_t SignalIndex = 0; SignalIndex < MAX_POSIX_SIGNALS_PER_PROCESS; ++SignalIndex)
    {
        ChildProcess->SignalActions[SignalIndex] = ParentProcess->SignalActions[SignalIndex];
    }

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        ChildProcess->MemoryMappings[MappingIndex] = ParentProcess->MemoryMappings[MappingIndex];
    }

    for (size_t FileIndex = 0; FileIndex < MAX_OPEN_FILES_PER_PROCESS; ++FileIndex)
    {
        if (ParentProcess->FileTable[FileIndex] == nullptr)
        {
            continue;
        }

        File* CopiedFile = new File;
        if (CopiedFile == nullptr)
        {
            PM->KillProcess(ChildId);
            return LINUX_ERR_ENOMEM;
        }

        *CopiedFile                        = *ParentProcess->FileTable[FileIndex];
        ChildProcess->FileTable[FileIndex] = CopiedFile;
    }

    ParentProcess->WaitingForVforkChild = true;
    ParentProcess->VforkChildId         = ChildId;

    ChildProcess->IsVforkChild  = true;
    ChildProcess->VforkParentId = ParentProcess->Id;

    Logic->AddProcessToReadyQueue(ChildId);

    if (ParentProcess->WaitingForSystemCallReturn && ParentProcess->HasSavedSystemCallFrame)
    {
        ParentProcess->State.cs = KERNEL_CS;
        ParentProcess->State.ss = KERNEL_SS;
    }

    Logic->BlockProcess(ParentProcess->Id);

    return static_cast<int64_t>(ChildId);
}

int64_t TranslationLayer::HandleExitGroupSystemCall(int64_t Status)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    bool    ExitingVforkChild = CurrentProcess->IsVforkChild;
    uint8_t VforkParentId     = CurrentProcess->VforkParentId;

    int32_t WaitStatus       = static_cast<int32_t>((static_cast<uint64_t>(Status) & 0xFFULL) << 8);
    uint8_t CurrentProcessId = CurrentProcess->Id;

#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr)
        {
            TTY* Terminal = ActiveDispatcher->GetResourceLayer()->GetTTY();
            if (Terminal != nullptr)
            {
                Terminal->Serialprintf("exit_group_dbg: enter pid=%u status=%lld wait_status=%d is_vfork=%u parent=%u waiting_sysret=%u saved_syscall=%u\n", CurrentProcessId,
                                       static_cast<long long>(Status), static_cast<int>(WaitStatus), ExitingVforkChild ? 1U : 0U, VforkParentId, CurrentProcess->WaitingForSystemCallReturn ? 1U : 0U,
                                       CurrentProcess->HasSavedSystemCallFrame ? 1U : 0U);
            }
        }
    }
#endif

    Logic->KillProcess(CurrentProcessId, WaitStatus);

#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr)
        {
            TTY* Terminal = ActiveDispatcher->GetResourceLayer()->GetTTY();
            if (Terminal != nullptr)
            {
                Process* KilledProcess    = PM->GetProcessById(CurrentProcessId);
                Process* CurrentAfterKill = PM->GetCurrentProcess();
                Terminal->Serialprintf("exit_group_dbg: after_kill pid=%u status=%s current_pid=%d\n", CurrentProcessId,
                                       (KilledProcess == nullptr) ? "<null>" : ((KilledProcess->Status == PROCESS_TERMINATED) ? "terminated" : "not-terminated"),
                                       (CurrentAfterKill == nullptr) ? -1 : static_cast<int>(CurrentAfterKill->Id));
            }
        }
    }
#endif

    if (ExitingVforkChild)
    {
        Process* ParentProcess = PM->GetProcessById(VforkParentId);
        if (ParentProcess != nullptr && ParentProcess->Status != PROCESS_TERMINATED)
        {
            bool ShouldUnblockParent            = ParentProcess->WaitingForVforkChild && ParentProcess->VforkChildId == CurrentProcessId;
            ParentProcess->WaitingForVforkChild = false;
            ParentProcess->VforkChildId         = PROCESS_ID_INVALID;
            if (ShouldUnblockParent)
            {
                Logic->UnblockProcess(ParentProcess->Id);
            }
        }
    }

    Logic->Schedule();

#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr)
        {
            TTY* Terminal = ActiveDispatcher->GetResourceLayer()->GetTTY();
            if (Terminal != nullptr)
            {
                Process* CurrentAfterSchedule = PM->GetCurrentProcess();
                Terminal->Serialprintf("exit_group_dbg: after_schedule current_pid=%d\n", (CurrentAfterSchedule == nullptr) ? -1 : static_cast<int>(CurrentAfterSchedule->Id));
            }
        }
    }
#endif

    while (true)
    {
        asm volatile("sti");
        X86Halt();
    }
}

int64_t TranslationLayer::HandleExecveSystemCall(const char* Path, const char* const* Argv, const char* const* Envp)
{
    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char* KernelPathBuffer = reinterpret_cast<char*>(Logic->kmalloc(SYSCALL_PATH_MAX));
    if (KernelPathBuffer == nullptr)
    {
        return LINUX_ERR_ENOMEM;
    }

    if (!CopyUserCString(Logic, Path, KernelPathBuffer, SYSCALL_PATH_MAX))
    {
        Logic->kfree(KernelPathBuffer);
        return LINUX_ERR_EFAULT;
    }

    char**   KernelArgv  = nullptr;
    char**   KernelEnvp  = nullptr;
    uint64_t KernelArgc  = 0;
    uint64_t KernelEnvc  = 0;
    bool     IsArgvValid = CopyUserStringVector(Logic, Argv, &KernelArgv, &KernelArgc);
    bool     IsEnvpValid = IsArgvValid && CopyUserStringVector(Logic, Envp, &KernelEnvp, &KernelEnvc);

#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr)
        {
            TTY* Terminal = ActiveDispatcher->GetResourceLayer()->GetTTY();
            if (Terminal != nullptr)
            {
                const char* Arg0 = (KernelArgv != nullptr && KernelArgc > 0 && KernelArgv[0] != nullptr) ? KernelArgv[0] : "<null>";
                const char* Arg1 = (KernelArgv != nullptr && KernelArgc > 1 && KernelArgv[1] != nullptr) ? KernelArgv[1] : "<null>";
                Terminal->Serialprintf("execve_dbg: path='%s' argc=%lu argv0='%s' argv1='%s' argv_ptr=%p envp_ptr=%p argv_ok=%d env_ok=%d\n", KernelPathBuffer, KernelArgc, Arg0, Arg1, (void*) Argv,
                                       (void*) Envp, IsArgvValid ? 1 : 0, IsEnvpValid ? 1 : 0);
            }
        }
    }
#endif

    if (!IsEnvpValid)
    {
        if (KernelArgv != nullptr)
        {
            FreeKernelStringVector(Logic, KernelArgv, KernelArgc);
        }

        if (KernelEnvp != nullptr)
        {
            FreeKernelStringVector(Logic, KernelEnvp, KernelEnvc);
        }

        Logic->kfree(KernelPathBuffer);
        return LINUX_ERR_EFAULT;
    }

    uint8_t ChangedProcessId = Logic->ChangeProcessExecution(CurrentProcess->Id, KernelPathBuffer, KernelArgv, KernelArgc, KernelEnvp, KernelEnvc);

    if (KernelArgv != nullptr)
    {
        FreeKernelStringVector(Logic, KernelArgv, KernelArgc);
    }

    if (KernelEnvp != nullptr)
    {
        FreeKernelStringVector(Logic, KernelEnvp, KernelEnvc);
    }

    Logic->kfree(KernelPathBuffer);

    if (ChangedProcessId == PROCESS_ID_INVALID)
    {
        return LINUX_ERR_ENOENT;
    }

    for (size_t FileIndex = 0; FileIndex < MAX_OPEN_FILES_PER_PROCESS; ++FileIndex)
    {
        File* OpenFile = CurrentProcess->FileTable[FileIndex];
        if (OpenFile == nullptr)
        {
            continue;
        }

        if ((OpenFile->DescriptorFlags & LINUX_FD_CLOEXEC) == 0)
        {
            continue;
        }

        delete OpenFile;
        CurrentProcess->FileTable[FileIndex] = nullptr;
    }

    ReleaseVforkParentIfNeeded(Logic, CurrentProcess);

    return 0;
}

int64_t TranslationLayer::HandleWaitSystemCall(int64_t Pid, int* Status, int64_t Options, void* Rusage)
{
    (void) Rusage;

    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    constexpr int64_t LINUX_WAIT_OPTION_WNOHANG    = 0x00000001;
    constexpr int64_t LINUX_WAIT_OPTION_WUNTRACED  = 0x00000002;
    constexpr int64_t LINUX_WAIT_OPTION_WCONTINUED = 0x00000008;
    constexpr int64_t LINUX_WAIT_OPTION_MASK       = LINUX_WAIT_OPTION_WNOHANG | LINUX_WAIT_OPTION_WUNTRACED | LINUX_WAIT_OPTION_WCONTINUED;

    if ((Options & ~LINUX_WAIT_OPTION_MASK) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    bool NoHang = (Options & LINUX_WAIT_OPTION_WNOHANG) != 0;

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    bool    WaitAnyChild      = (Pid == -1);
    uint8_t RequestedChildPid = PROCESS_ID_INVALID;
    if (!WaitAnyChild)
    {
        if (Pid < 0 || Pid > 255)
        {
            return LINUX_ERR_ECHILD;
        }

        RequestedChildPid = static_cast<uint8_t>(Pid);
    }

    auto IsMatchingChildId = [&](uint8_t ChildId) -> bool { return WaitAnyChild || ChildId == RequestedChildPid; };

    auto HasMatchingLiveChild = [&]() -> bool
    {
        for (size_t Index = 0; Index < PM->GetMaxProcesses(); ++Index)
        {
            Process* CandidateProcess = PM->GetProcessById(static_cast<uint8_t>(Index));
            if (CandidateProcess == nullptr)
            {
                continue;
            }

            if (CandidateProcess->ParrentId != CurrentProcess->Id || CandidateProcess->Status == PROCESS_TERMINATED)
            {
                continue;
            }

            if (IsMatchingChildId(CandidateProcess->Id))
            {
                return true;
            }
        }

        return false;
    };

    auto ConsumePendingChild = [&]() -> int64_t
    {
        if (!CurrentProcess->HasPendingChildExit)
        {
            return PROCESS_ID_INVALID;
        }

        if (!IsMatchingChildId(CurrentProcess->PendingChildId))
        {
            return PROCESS_ID_INVALID;
        }

        int32_t ChildExitStatus = CurrentProcess->PendingChildStatus;
        if (Status != nullptr && !Logic->CopyFromKernelToUser(&ChildExitStatus, Status, sizeof(ChildExitStatus)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint8_t ExitedChildId               = CurrentProcess->PendingChildId;
        CurrentProcess->HasPendingChildExit = false;
        CurrentProcess->PendingChildId      = PROCESS_ID_INVALID;
        CurrentProcess->PendingChildStatus  = 0;
        CurrentProcess->WaitingForChild     = false;
        return static_cast<int64_t>(ExitedChildId);
    };

    int64_t ImmediateResult = ConsumePendingChild();
    if (ImmediateResult != PROCESS_ID_INVALID)
    {
        return ImmediateResult;
    }

    if (!HasMatchingLiveChild())
    {
        return LINUX_ERR_ECHILD;
    }

    if (NoHang)
    {
        return 0;
    }

    CurrentProcess->WaitingForChild = true;
    Logic->BlockProcess(CurrentProcess->Id);

    int64_t ResultAfterWake = ConsumePendingChild();
    if (ResultAfterWake != PROCESS_ID_INVALID)
    {
        return ResultAfterWake;
    }

    CurrentProcess->WaitingForChild = false;
    return HasMatchingLiveChild() ? 0 : LINUX_ERR_ECHILD;
}

int64_t TranslationLayer::HandleMmapSystemCall(void* Address, uint64_t Length, int64_t Protection, int64_t Flags, int64_t FileDescriptor, int64_t Offset)
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();

    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Length == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Offset < 0 || (static_cast<uint64_t>(Offset) & (PAGE_SIZE - 1)) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    int64_t SharingType = Flags & (LINUX_MAP_PRIVATE | LINUX_MAP_SHARED);
    if (SharingType == 0 || SharingType == (LINUX_MAP_PRIVATE | LINUX_MAP_SHARED))
    {
        return LINUX_ERR_EINVAL;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER || CurrentProcess->AddressSpace == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t MappingLength = AlignUpToPageBoundary(Length);
    if (MappingLength == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t RequestedAddress = reinterpret_cast<uint64_t>(Address);
    uint64_t MappingStart     = 0;

    if ((Flags & LINUX_MAP_FIXED) != 0)
    {
        if ((RequestedAddress & (PAGE_SIZE - 1)) != 0)
        {
            return LINUX_ERR_EINVAL;
        }

        MappingStart = RequestedAddress;
        if (MappingOverlapsReservedProcessLayout(CurrentProcess, MappingStart, MappingLength))
        {
            return LINUX_ERR_EINVAL;
        }

        int64_t UnmapResult = HandleMunmapSystemCall(reinterpret_cast<void*>(MappingStart), MappingLength);
        if (UnmapResult < 0)
        {
            return UnmapResult;
        }
    }
    else
    {
        MappingStart = FindFreeMappingAddress(CurrentProcess, RequestedAddress, MappingLength);
        if (MappingStart == 0)
        {
            return LINUX_ERR_ENOMEM;
        }
    }

    if ((Flags & LINUX_MAP_ANONYMOUS) != 0)
    {
        if (ActiveDispatcher == nullptr || ActiveDispatcher->GetResourceLayer() == nullptr || ActiveDispatcher->GetResourceLayer()->GetPMM() == nullptr)
        {
            return LINUX_ERR_EFAULT;
        }

        PhysicalMemoryManager* PMM                = ActiveDispatcher->GetResourceLayer()->GetPMM();
        uint64_t               PageCount          = MappingLength / PAGE_SIZE;
        void*                  PhysicalAllocation = PMM->AllocatePagesFromDescriptor(PageCount);
        if (PhysicalAllocation == nullptr)
        {
            return LINUX_ERR_ENOMEM;
        }

        kmemset(PhysicalAllocation, 0, static_cast<size_t>(PageCount * PAGE_SIZE));

        uint64_t PageMapL4TableAddr = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
        if (PageMapL4TableAddr == 0)
        {
            PMM->FreePagesFromDescriptor(PhysicalAllocation, PageCount);
            return LINUX_ERR_EFAULT;
        }

        VirtualMemoryManager UserVMM(PageMapL4TableAddr, *PMM);
        bool                 Writable = (Protection & LINUX_PROT_WRITE) != 0;

        for (uint64_t PageIndex = 0; PageIndex < PageCount; ++PageIndex)
        {
            uint64_t PhysicalPage = reinterpret_cast<uint64_t>(PhysicalAllocation) + (PageIndex * PAGE_SIZE);
            uint64_t VirtualPage  = MappingStart + (PageIndex * PAGE_SIZE);
            if (!UserVMM.MapPage(PhysicalPage, VirtualPage, PageMappingFlags(true, Writable)))
            {
                PMM->FreePagesFromDescriptor(PhysicalAllocation, PageCount);
                return LINUX_ERR_EFAULT;
            }
        }

        if (!RegisterProcessMapping(CurrentProcess, MappingStart, MappingLength, reinterpret_cast<uint64_t>(PhysicalAllocation), true))
        {
            PMM->FreePagesFromDescriptor(PhysicalAllocation, PageCount);
            return LINUX_ERR_ENOMEM;
        }

        uint64_t ActivePageTable = ActiveDispatcher->GetResourceLayer()->ReadCurrentPageTable();
        if (ActivePageTable == PageMapL4TableAddr)
        {
            ActiveDispatcher->GetResourceLayer()->LoadPageTable(ActivePageTable);
        }

        return static_cast<int64_t>(MappingStart);
    }

    if (FileDescriptor < 0 || static_cast<uint64_t>(FileDescriptor) >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[static_cast<size_t>(FileDescriptor)];
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->Node->FileOps == nullptr || OpenFile->Node->FileOps->MemoryMap == nullptr)
    {
        return LINUX_ERR_ENODEV;
    }

    uint64_t FileMappedAddress = 0;
    int64_t  MappingResult     = OpenFile->Node->FileOps->MemoryMap(OpenFile, Length, static_cast<uint64_t>(Offset), CurrentProcess->AddressSpace, &FileMappedAddress);
    if (MappingResult == LINUX_ERR_ENOSYS)
    {
        if (ActiveDispatcher == nullptr || ActiveDispatcher->GetResourceLayer() == nullptr || ActiveDispatcher->GetResourceLayer()->GetPMM() == nullptr)
        {
            return LINUX_ERR_EFAULT;
        }

        if (OpenFile->Node->FileOps->Read == nullptr)
        {
            return LINUX_ERR_ENOSYS;
        }

        PhysicalMemoryManager* PMM                = ActiveDispatcher->GetResourceLayer()->GetPMM();
        uint64_t               PageCount          = MappingLength / PAGE_SIZE;
        void*                  PhysicalAllocation = PMM->AllocatePagesFromDescriptor(PageCount);
        if (PhysicalAllocation == nullptr)
        {
            return LINUX_ERR_ENOMEM;
        }

        kmemset(PhysicalAllocation, 0, static_cast<size_t>(PageCount * PAGE_SIZE));

        uint64_t OriginalOffset = OpenFile->CurrentOffset;
        OpenFile->CurrentOffset = static_cast<uint64_t>(Offset);

        uint8_t* DestinationBuffer = reinterpret_cast<uint8_t*>(PhysicalAllocation);
        uint64_t RemainingBytes    = Length;
        while (RemainingBytes > 0)
        {
            int64_t ReadResult = OpenFile->Node->FileOps->Read(OpenFile, DestinationBuffer, RemainingBytes);
            if (ReadResult < 0)
            {
                OpenFile->CurrentOffset = OriginalOffset;
                PMM->FreePagesFromDescriptor(PhysicalAllocation, PageCount);
                return ReadResult;
            }

            if (ReadResult == 0)
            {
                break;
            }

            DestinationBuffer += static_cast<uint64_t>(ReadResult);
            RemainingBytes -= static_cast<uint64_t>(ReadResult);
        }

        OpenFile->CurrentOffset = OriginalOffset;

        uint64_t PageMapL4TableAddr = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
        if (PageMapL4TableAddr == 0)
        {
            PMM->FreePagesFromDescriptor(PhysicalAllocation, PageCount);
            return LINUX_ERR_EFAULT;
        }

        VirtualMemoryManager UserVMM(PageMapL4TableAddr, *PMM);
        bool                 Writable = (Protection & LINUX_PROT_WRITE) != 0;

        for (uint64_t PageIndex = 0; PageIndex < PageCount; ++PageIndex)
        {
            uint64_t PhysicalPage = reinterpret_cast<uint64_t>(PhysicalAllocation) + (PageIndex * PAGE_SIZE);
            uint64_t VirtualPage  = MappingStart + (PageIndex * PAGE_SIZE);
            if (!UserVMM.MapPage(PhysicalPage, VirtualPage, PageMappingFlags(true, Writable)))
            {
                PMM->FreePagesFromDescriptor(PhysicalAllocation, PageCount);
                return LINUX_ERR_EFAULT;
            }
        }

        if (!RegisterProcessMapping(CurrentProcess, MappingStart, MappingLength, reinterpret_cast<uint64_t>(PhysicalAllocation), true))
        {
            PMM->FreePagesFromDescriptor(PhysicalAllocation, PageCount);
            return LINUX_ERR_ENOMEM;
        }

        uint64_t ActivePageTable = ActiveDispatcher->GetResourceLayer()->ReadCurrentPageTable();
        if (ActivePageTable == PageMapL4TableAddr)
        {
            ActiveDispatcher->GetResourceLayer()->LoadPageTable(ActivePageTable);
        }

        return static_cast<int64_t>(MappingStart);
    }

    if (MappingResult < 0)
    {
        return MappingResult;
    }

    if (FileMappedAddress == 0)
    {
        return LINUX_ERR_EFAULT;
    }

    uint64_t RegisteredStart  = AlignDownToPageBoundary(FileMappedAddress);
    uint64_t RegisteredLength = AlignUpToPageBoundary((FileMappedAddress - RegisteredStart) + Length);
    if (RegisteredLength == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    if (!RegisterProcessMapping(CurrentProcess, RegisteredStart, RegisteredLength, 0, false))
    {
        return LINUX_ERR_ENOMEM;
    }

    if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr)
    {
        uint64_t UserPageTable   = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
        uint64_t ActivePageTable = ActiveDispatcher->GetResourceLayer()->ReadCurrentPageTable();
        if (UserPageTable != 0 && ActivePageTable == UserPageTable)
        {
            ActiveDispatcher->GetResourceLayer()->LoadPageTable(ActivePageTable);
        }
    }

    return static_cast<int64_t>(FileMappedAddress);
}

int64_t TranslationLayer::HandleMunmapSystemCall(void* Address, uint64_t Length)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Length == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t UnmapStart = reinterpret_cast<uint64_t>(Address);
    if ((UnmapStart & (PAGE_SIZE - 1)) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t UnmapLength = AlignUpToPageBoundary(Length);
    if (UnmapLength == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t UnmapEnd = UnmapStart + UnmapLength;
    if (UnmapEnd < UnmapStart)
    {
        return LINUX_ERR_EINVAL;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER || CurrentProcess->AddressSpace == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    size_t ResultingInUseMappings = 0;

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        const ProcessMemoryMapping& Mapping = CurrentProcess->MemoryMappings[MappingIndex];
        if (!Mapping.InUse)
        {
            continue;
        }

        uint64_t MappingStart = Mapping.VirtualAddressStart;
        uint64_t MappingEnd   = MappingStart + Mapping.Length;
        if (MappingEnd <= MappingStart)
        {
            continue;
        }

        uint64_t OverlapStart = (UnmapStart > MappingStart) ? UnmapStart : MappingStart;
        uint64_t OverlapEnd   = (UnmapEnd < MappingEnd) ? UnmapEnd : MappingEnd;

        if (OverlapStart >= OverlapEnd)
        {
            ++ResultingInUseMappings;
            continue;
        }

        bool TrimsLeft  = (OverlapStart > MappingStart);
        bool TrimsRight = (OverlapEnd < MappingEnd);

        if (TrimsLeft && TrimsRight)
        {
            ResultingInUseMappings += 2;
        }
        else if (TrimsLeft || TrimsRight)
        {
            ResultingInUseMappings += 1;
        }
    }

    if (ResultingInUseMappings > MAX_MEMORY_MAPPINGS_PER_PROCESS)
    {
        return LINUX_ERR_ENOMEM;
    }

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr || ActiveDispatcher->GetResourceLayer() == nullptr || ActiveDispatcher->GetResourceLayer()->GetPMM() == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ResourceLayer*         Resource = ActiveDispatcher->GetResourceLayer();
    PhysicalMemoryManager* PMM      = Resource->GetPMM();

    uint64_t UserPageTable = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
    if (UserPageTable == 0)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualMemoryManager UserVMM(UserPageTable, *PMM);

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        ProcessMemoryMapping& Mapping = CurrentProcess->MemoryMappings[MappingIndex];
        if (!Mapping.InUse)
        {
            continue;
        }

        ProcessMemoryMapping OriginalMapping = Mapping;

        uint64_t MappingStart = OriginalMapping.VirtualAddressStart;
        uint64_t MappingEnd   = MappingStart + OriginalMapping.Length;
        if (MappingEnd <= MappingStart)
        {
            continue;
        }

        uint64_t OverlapStart = (UnmapStart > MappingStart) ? UnmapStart : MappingStart;
        uint64_t OverlapEnd   = (UnmapEnd < MappingEnd) ? UnmapEnd : MappingEnd;

        if (OverlapStart >= OverlapEnd)
        {
            continue;
        }

        uint64_t OverlapLength = OverlapEnd - OverlapStart;

        if (OriginalMapping.IsAnonymous && OriginalMapping.PhysicalAddressStart != 0)
        {
            uint64_t PhysicalOverlapStart = OriginalMapping.PhysicalAddressStart + (OverlapStart - MappingStart);
            uint64_t OverlapPages         = OverlapLength / PAGE_SIZE;
            if (OverlapPages != 0)
            {
                PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(PhysicalOverlapStart), OverlapPages);
            }
        }

        for (uint64_t VirtualAddress = OverlapStart; VirtualAddress < OverlapEnd; VirtualAddress += PAGE_SIZE)
        {
            UserVMM.UnmapPage(VirtualAddress);
        }

        bool TrimsLeft  = (OverlapStart > MappingStart);
        bool TrimsRight = (OverlapEnd < MappingEnd);

        if (!TrimsLeft && !TrimsRight)
        {
            Mapping = {};
            continue;
        }

        if (!TrimsLeft && TrimsRight)
        {
            uint64_t NewLength          = MappingEnd - OverlapEnd;
            Mapping.VirtualAddressStart = OverlapEnd;
            Mapping.Length              = NewLength;
            if (Mapping.PhysicalAddressStart != 0)
            {
                Mapping.PhysicalAddressStart = OriginalMapping.PhysicalAddressStart + (OverlapEnd - MappingStart);
            }
            continue;
        }

        if (TrimsLeft && !TrimsRight)
        {
            Mapping.Length = OverlapStart - MappingStart;
            continue;
        }

        int64_t FreeSlot = FindAvailableMappingSlot(CurrentProcess);
        if (FreeSlot < 0)
        {
            return LINUX_ERR_ENOMEM;
        }

        ProcessMemoryMapping& RightMapping = CurrentProcess->MemoryMappings[static_cast<size_t>(FreeSlot)];
        RightMapping                       = OriginalMapping;
        RightMapping.InUse                 = true;
        RightMapping.VirtualAddressStart   = OverlapEnd;
        RightMapping.Length                = MappingEnd - OverlapEnd;
        if (RightMapping.PhysicalAddressStart != 0)
        {
            RightMapping.PhysicalAddressStart = OriginalMapping.PhysicalAddressStart + (OverlapEnd - MappingStart);
        }

        Mapping.VirtualAddressStart = MappingStart;
        Mapping.Length              = OverlapStart - MappingStart;
    }

    uint64_t ActivePageTable = Resource->ReadCurrentPageTable();
    if (ActivePageTable == UserPageTable)
    {
        Resource->LoadPageTable(ActivePageTable);
    }

    return 0;
}

int64_t TranslationLayer::HandleArchPrctlSystemCall(uint64_t Code, uint64_t Address)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Code == LINUX_ARCH_SET_FS)
    {
        if (!IsCanonicalX86_64Address(Address))
        {
            return LINUX_ERR_EPERM;
        }

        CurrentProcess->UserFSBase = Address;
        SetUserFSBase(Address);
        return 0;
    }

    if (Code == LINUX_ARCH_GET_FS)
    {
        if (Address == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t CurrentFSBase = CurrentProcess->UserFSBase;
        if (!Logic->CopyFromKernelToUser(&CurrentFSBase, reinterpret_cast<void*>(Address), sizeof(CurrentFSBase)))
        {
            return LINUX_ERR_EFAULT;
        }

        return 0;
    }

    return LINUX_ERR_EINVAL;
}

int64_t TranslationLayer::HandleRtSigactionSystemCall(int64_t Signal, const void* Action, void* OldAction, uint64_t SigsetSize)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER)
    {
        return LINUX_ERR_EINVAL;
    }

    if (SigsetSize != LINUX_RT_SIGSET_SIZE)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Signal < LINUX_SIGNAL_MIN || Signal > LINUX_SIGNAL_MAX)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Signal == LINUX_SIGNAL_SIGKILL || Signal == LINUX_SIGNAL_SIGSTOP)
    {
        return LINUX_ERR_EINVAL;
    }

    size_t SignalIndex = static_cast<size_t>(Signal - 1);

    LinuxKernelSigAction OldKernelAction = {};
    OldKernelAction.Handler              = CurrentProcess->SignalActions[SignalIndex].Handler;
    OldKernelAction.Flags                = CurrentProcess->SignalActions[SignalIndex].Flags;
    OldKernelAction.Restorer             = CurrentProcess->SignalActions[SignalIndex].Restorer;
    OldKernelAction.Mask                 = CurrentProcess->SignalActions[SignalIndex].Mask;

    if (OldAction != nullptr)
    {
        if (!Logic->CopyFromKernelToUser(&OldKernelAction, OldAction, LINUX_RT_SIGACTION_SIZE))
        {
            return LINUX_ERR_EFAULT;
        }
    }

    if (Action != nullptr)
    {
        LinuxKernelSigAction NewKernelAction = {};
        if (!Logic->CopyFromUserToKernel(Action, &NewKernelAction, LINUX_RT_SIGACTION_SIZE))
        {
            return LINUX_ERR_EFAULT;
        }

        NewKernelAction.Mask &= ~LINUX_UNBLOCKABLE_SIGNAL_MASK;

        CurrentProcess->SignalActions[SignalIndex].Handler  = NewKernelAction.Handler;
        CurrentProcess->SignalActions[SignalIndex].Flags    = NewKernelAction.Flags;
        CurrentProcess->SignalActions[SignalIndex].Restorer = NewKernelAction.Restorer;
        CurrentProcess->SignalActions[SignalIndex].Mask     = NewKernelAction.Mask;

        if (CurrentProcess->SignalActions[SignalIndex].Handler == static_cast<uint64_t>(LINUX_SIG_DFL) || CurrentProcess->SignalActions[SignalIndex].Handler == static_cast<uint64_t>(LINUX_SIG_IGN))
        {
            return 0;
        }
    }

    return 0;
}

int64_t TranslationLayer::HandleRtSigprocmaskSystemCall(int64_t How, const void* Set, void* OldSet, uint64_t SigsetSize)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER)
    {
        return LINUX_ERR_EINVAL;
    }

    if (SigsetSize != LINUX_RT_SIGSET_SIZE)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t RequestedMask    = 0;
    bool     ShouldUpdateMask = (Set != nullptr);
    if (ShouldUpdateMask)
    {
        if (!Logic->CopyFromUserToKernel(Set, &RequestedMask, sizeof(RequestedMask)))
        {
            return LINUX_ERR_EFAULT;
        }

        RequestedMask &= ~LINUX_UNBLOCKABLE_SIGNAL_MASK;

        if (How == LINUX_SIG_BLOCK)
        {
            RequestedMask |= CurrentProcess->BlockedSignalMask;
        }
        else if (How == LINUX_SIG_UNBLOCK)
        {
            RequestedMask = CurrentProcess->BlockedSignalMask & ~RequestedMask;
        }
        else if (How != LINUX_SIG_SETMASK)
        {
            return LINUX_ERR_EINVAL;
        }
    }

    uint64_t PreviousMask = CurrentProcess->BlockedSignalMask;
    if (ShouldUpdateMask)
    {
        CurrentProcess->BlockedSignalMask = RequestedMask;
    }

    if (OldSet != nullptr && !Logic->CopyFromKernelToUser(&PreviousMask, OldSet, sizeof(PreviousMask)))
    {
        return LINUX_ERR_EFAULT;
    }

    return 0;
}

int64_t TranslationLayer::HandleRtSigsuspendSystemCall(const void* Set, uint64_t SigsetSize)
{
    if (Logic == nullptr || Set == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER)
    {
        return LINUX_ERR_EINVAL;
    }

    if (SigsetSize != LINUX_RT_SIGSET_SIZE)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t TemporaryMask = 0;
    if (!Logic->CopyFromUserToKernel(Set, &TemporaryMask, sizeof(TemporaryMask)))
    {
        return LINUX_ERR_EFAULT;
    }

    TemporaryMask &= ~LINUX_UNBLOCKABLE_SIGNAL_MASK;

    uint64_t PreviousMask             = CurrentProcess->BlockedSignalMask;
    CurrentProcess->BlockedSignalMask = TemporaryMask;

    Logic->BlockProcess(CurrentProcess->Id);

    CurrentProcess->BlockedSignalMask = PreviousMask;
    return LINUX_ERR_EINTR;
}

int64_t TranslationLayer::HandleSetTidAddressSystemCall(int* TidPointer)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER)
    {
        return LINUX_ERR_EINVAL;
    }

    CurrentProcess->ClearChildTidAddress = TidPointer;
    return static_cast<int64_t>(CurrentProcess->Id);
}