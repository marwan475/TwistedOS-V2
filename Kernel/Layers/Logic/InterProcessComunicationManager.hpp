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
    LINUX_SOCKET_ERR_EOPNOTSUPP  = -95,
    LINUX_SOCKET_ERR_ENOMEM      = -12,
    LINUX_SOCKET_ERR_EAGAIN      = -11,
    LINUX_SOCKET_ERR_EMFILE      = -24,
    LINUX_SOCKET_ERR_EBADF       = -9,
    LINUX_SOCKET_ERR_ENOTSOCK    = -88,
    LINUX_SOCKET_ERR_EISCONN     = -106,
    LINUX_SOCKET_ERR_ECONNREFUSED = -111,
    LINUX_SOCKET_ERR_EADDRINUSE  = -98,
    LINUX_SOCKET_ERR_ENAMETOOLONG = -36
};

struct LinuxSockAddr
{
    uint16_t Family;
    char     Data[14];
};

struct LinuxSockAddrIn
{
    uint16_t Family;
    uint16_t Port;
    uint32_t Address;
    uint8_t  Zero[8];
};

struct LinuxSockAddrUn
{
    uint16_t Family;
    char     Path[108];
};

struct NetworkSocket{
    uint32_t LocalIp;
    uint16_t LocalPort;
    uint32_t RemoteIp;
    uint16_t RemotePort;
    bool     IsBound;
    bool     IsListening;
    int32_t  ListenBacklog;
};

struct Socket;

struct UnixSocket{
    char*    Path;
    uint64_t PathLength;
    bool     IsBound;
    bool     IsListening;
    int32_t  ListenBacklog;
    Socket** PendingConnections;
    uint64_t PendingConnectionCount;
    uint64_t PendingConnectionCapacity;
    Socket*  ConnectedPeer;
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
    int64_t BindSocket(Process* Owner, int64_t FileDescriptor, const void* SocketAddress, uint64_t SocketAddressLength);
    int64_t ListenSocket(Process* Owner, int64_t FileDescriptor, int64_t Backlog);
    int64_t ConnectSocket(Process* Owner, int64_t FileDescriptor, const void* SocketAddress, uint64_t SocketAddressLength);
    Socket* AcceptSocket(Process* Owner, int64_t FileDescriptor, Process* AcceptedOwner, int64_t AcceptedFileDescriptor, int64_t* ErrorCode = nullptr);
    bool    CloseSocket(Process* Owner, int64_t FileDescriptor);
    uint64_t GetSocketCount() const;
};
