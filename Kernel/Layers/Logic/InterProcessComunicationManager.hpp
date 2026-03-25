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
    LINUX_AF_INET = 2,
    LINUX_AF_NETLINK = 16
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
    LINUX_SOCKET_ERR_ENOTCONN    = -107,
    LINUX_SOCKET_ERR_ECONNREFUSED = -111,
    LINUX_SOCKET_ERR_EPIPE       = -32,
    LINUX_SOCKET_ERR_EADDRINUSE  = -98,
    LINUX_SOCKET_ERR_ENAMETOOLONG = -36
};

enum LinuxPipeErrors
{
    LINUX_PIPE_ERR_EINVAL = -22,
    LINUX_PIPE_ERR_ENOMEM = -12,
    LINUX_PIPE_ERR_EAGAIN = -11,
    LINUX_PIPE_ERR_EMFILE = -24,
    LINUX_PIPE_ERR_EBADF  = -9,
    LINUX_PIPE_ERR_EPIPE  = -32
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

struct LinuxSockAddrNl
{
    uint16_t Family;
    uint16_t Pad;
    uint32_t Pid;
    uint32_t Groups;
};

struct NetworkSocket{
    uint32_t LocalIp;
    uint16_t LocalPort;
    uint32_t RemoteIp;
    uint16_t RemotePort;
    bool     IsBound;
    bool     IsListening;
    int32_t  ListenBacklog;
    bool     IsShutdownRead;
    bool     IsShutdownWrite;
};

struct NetlinkSocket{
    uint32_t LocalPid;
    uint32_t LocalGroups;
    uint32_t RemotePid;
    uint32_t RemoteGroups;
    bool     IsBound;
    bool     IsShutdownRead;
    bool     IsShutdownWrite;
};

struct Socket;

struct UnixSocket{
    char*    Path;
    uint64_t PathLength;
    bool     IsAbstract;
    bool     IsBound;
    bool     IsListening;
    int32_t  ListenBacklog;
    Socket** PendingConnections;
    uint64_t PendingConnectionCount;
    uint64_t PendingConnectionCapacity;
    Socket*  ConnectedPeer;
    bool     IsShutdownRead;
    bool     IsShutdownWrite;
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
    static constexpr uint64_t MAX_TRACKED_PIPES   = MAX_PROCESSES * MAX_OPEN_FILES_PER_PROCESS;

    Socket*  SocketList[MAX_TRACKED_SOCKETS];
    uint64_t SocketCount;

    struct TrackedPipeEndpoint
    {
        Process* Owner;
        int64_t  FileDescriptor;
        INode*   Node;
        void*    Endpoint;
    };

    TrackedPipeEndpoint PipeEndpointList[MAX_TRACKED_PIPES];
    uint64_t            PipeEndpointCount;

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
    int64_t ShutdownSocket(Process* Owner, int64_t FileDescriptor, int64_t How);
    bool    CloseSocket(Process* Owner, int64_t FileDescriptor);
    uint64_t GetSocketCount() const;

    int64_t CreatePipe(Process* Owner, int64_t ReadFileDescriptor, int64_t WriteFileDescriptor, INode** ReadNodeOut, INode** WriteNodeOut);
    int64_t DuplicatePipeDescriptor(Process* Owner, int64_t FileDescriptor, const File* SourceFile);
    bool    ClosePipe(Process* Owner, int64_t FileDescriptor, const INode* Node);
};
