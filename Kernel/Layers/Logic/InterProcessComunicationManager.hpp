/**
 * File: InterProcessComunicationManager.hpp
 * Author: Marwan Mostafa
 * Description: Inter-process communication manager declarations.
 */

#pragma once

#include "ProcessManager.hpp"

#include <stdint.h>

enum LinuxSocketDomain
{
    LINUX_AF_UNIX = 1,
    LINUX_AF_INET = 2
};

enum LinuxSocketType
{
    LINUX_SOCK_STREAM    = 1,
    LINUX_SOCK_DGRAM     = 2,
    LINUX_SOCK_RAW       = 3,
    LINUX_SOCK_SEQPACKET = 5
};

enum LinuxSocketFlags
{
    LINUX_SOCK_TYPE_MASK = 0xF,
    LINUX_SOCK_NONBLOCK  = 0x800,
    LINUX_SOCK_CLOEXEC   = 0x80000
};

enum LinuxSocketErrors
{
    LINUX_SOCKET_ERR_EINVAL      = -22,
    LINUX_SOCKET_ERR_EAFNOSUPPORT = -97,
    LINUX_SOCKET_ERR_ESOCKTNOSUPPORT = -94,
    LINUX_SOCKET_ERR_ENOMEM      = -12,
    LINUX_SOCKET_ERR_EMFILE      = -24,
    LINUX_SOCKET_ERR_EBADF       = -9
};

struct NetworkSocket{
    uint32_t LocalIp;
    uint16_t LocalPort;
    uint32_t RemoteIp;
    uint16_t RemotePort;
};

struct UnixSocket{
    char* Path;
};

struct Socket{
    int32_t        Domain;
    int32_t        Type;
    int32_t        Protocol;
    Process*       Owner;
    int64_t        FileDescriptor;
    void*          Implementation;
    FileOperations FileOps;
};

class InterProcessComunicationManager
{
private:
    static constexpr uint64_t MAX_TRACKED_SOCKETS = MAX_PROCESSES * MAX_OPEN_FILES_PER_PROCESS;

    Socket*  SocketList[MAX_TRACKED_SOCKETS];
    uint64_t SocketCount;

    void InitializeSocketFileOperations(Socket* SocketEntry);
    void DestroySocket(Socket* SocketEntry);

public:
    InterProcessComunicationManager();
    ~InterProcessComunicationManager();

    Socket* CreateSocket(int64_t Domain, int64_t Type, int64_t Protocol, Process* Owner, int64_t FileDescriptor, int64_t* ErrorCode = nullptr);
    bool    CloseSocket(Process* Owner, int64_t FileDescriptor);
    uint64_t GetSocketCount() const;
};
