/**
 * File: InterProcessComunicationManager.cpp
 * Author: Marwan Mostafa
 * Description: Inter-process communication manager implementation.
 */

#include "InterProcessComunicationManager.hpp"

namespace
{
constexpr uint64_t LINUX_SOCKADDR_FAMILY_SIZE = sizeof(uint16_t);
constexpr int64_t  LINUX_LISTEN_SOMAXCONN     = 4096;
constexpr uint16_t LINUX_INET_EPHEMERAL_START = 49152;
constexpr uint16_t LINUX_INET_EPHEMERAL_END   = 65535;
constexpr int64_t  LINUX_SHUT_RD              = 0;
constexpr int64_t  LINUX_SHUT_WR              = 1;
constexpr int64_t  LINUX_SHUT_RDWR            = 2;
constexpr uint64_t LINUX_PIPE_BUFFER_CAPACITY = 4096;

constexpr uint32_t LINUX_POLLIN  = 0x0001u;
constexpr uint32_t LINUX_POLLOUT = 0x0004u;
constexpr uint32_t LINUX_POLLERR = 0x0008u;
constexpr uint32_t LINUX_POLLHUP = 0x0010u;

struct PipeEndpoint;

struct PipeKernelObject
{
	uint8_t       Buffer[LINUX_PIPE_BUFFER_CAPACITY];
	uint64_t      ReadOffset;
	uint64_t      WriteOffset;
	uint64_t      BufferedBytes;
	uint64_t      ReaderReferenceCount;
	uint64_t      WriterReferenceCount;
	PipeEndpoint* ReadEndpoint;
	PipeEndpoint* WriteEndpoint;
	INode*        ReadNode;
	INode*        WriteNode;
};

struct PipeEndpoint
{
	PipeKernelObject* Pipe;
	bool              IsReadEnd;
};

uint16_t ReadLinuxAddressFamily(const void* SocketAddress)
{
	if (SocketAddress == nullptr)
	{
		return 0;
	}

	const uint8_t* AddressBytes = reinterpret_cast<const uint8_t*>(SocketAddress);
	return static_cast<uint16_t>(static_cast<uint16_t>(AddressBytes[0]) | (static_cast<uint16_t>(AddressBytes[1]) << 8));
}

uint64_t ComputeUnixAddressPathLength(const char* Path, uint64_t MaxLength, bool* IsAbstract)
{
	if (IsAbstract != nullptr)
	{
		*IsAbstract = false;
	}

	if (Path == nullptr)
	{
		return 0;
	}

	if (MaxLength == 0)
	{
		return 0;
	}

	if (Path[0] == '\0')
	{
		if (IsAbstract != nullptr)
		{
			*IsAbstract = true;
		}

		if (MaxLength <= 1)
		{
			return 0;
		}

		return MaxLength;
	}

	uint64_t Length = 0;
	while (Length < MaxLength && Path[Length] != '\0')
	{
		++Length;
	}

	return Length;
}

uint64_t GetComparableUnixPathLength(const char* Path, uint64_t PathLength, bool IsAbstract)
{
	if (Path == nullptr)
	{
		return 0;
	}

	if (!IsAbstract)
	{
		return PathLength;
	}

	while (PathLength > 0 && Path[PathLength - 1] == '\0')
	{
		--PathLength;
	}

	return PathLength;
}

bool AreUnixPathsEqual(const char* LeftPath, uint64_t LeftPathLength, const char* RightPath, uint64_t RightPathLength, bool IsAbstract)
{
	if (LeftPath == nullptr || RightPath == nullptr)
	{
		return false;
	}

	LeftPathLength  = GetComparableUnixPathLength(LeftPath, LeftPathLength, IsAbstract);
	RightPathLength = GetComparableUnixPathLength(RightPath, RightPathLength, IsAbstract);

	if (LeftPathLength != RightPathLength)
	{
		return false;
	}

	for (uint64_t Index = 0; Index < LeftPathLength; ++Index)
	{
		if (LeftPath[Index] != RightPath[Index])
		{
			return false;
		}
	}

	return true;
}

bool IsSocketTracked(Socket* const* SocketList, uint64_t SocketCount, const Socket* Candidate)
{
	if (Candidate == nullptr)
	{
		return false;
	}

	for (uint64_t SocketIndex = 0; SocketIndex < SocketCount; ++SocketIndex)
	{
		if (SocketList[SocketIndex] == Candidate)
		{
			return true;
		}
	}

	return false;
}

void RemovePendingConnectionFromUnixQueue(UnixSocket* QueueOwner, const Socket* Target)
{
	if (QueueOwner == nullptr || QueueOwner->PendingConnections == nullptr || QueueOwner->PendingConnectionCount == 0 || Target == nullptr)
	{
		return;
	}

	uint64_t WriteIndex = 0;
	for (uint64_t ReadIndex = 0; ReadIndex < QueueOwner->PendingConnectionCount; ++ReadIndex)
	{
		Socket* Entry = QueueOwner->PendingConnections[ReadIndex];
		if (Entry == Target)
		{
			continue;
		}

		QueueOwner->PendingConnections[WriteIndex++] = Entry;
	}

	QueueOwner->PendingConnectionCount = WriteIndex;
}

Socket* GetSocketFromOpenFile(File* OpenFile)
{
	if (OpenFile == nullptr || OpenFile->Node == nullptr)
	{
		return nullptr;
	}

	return reinterpret_cast<Socket*>(OpenFile->Node->NodeData);
}

int64_t SocketRead(File* OpenFile, void* Buffer, uint64_t Count)
{
	(void) Buffer;
	(void) Count;

	Socket* SocketEntry = GetSocketFromOpenFile(OpenFile);
	if (SocketEntry == nullptr)
	{
		return LINUX_SOCKET_ERR_EBADF;
	}

	if (SocketEntry->Domain == LINUX_AF_UNIX)
	{
		UnixSocket* SocketImplementation = reinterpret_cast<UnixSocket*>(SocketEntry->Implementation);
		if (SocketImplementation != nullptr && SocketImplementation->IsShutdownRead)
		{
			return 0;
		}
	}
	else if (SocketEntry->Domain == LINUX_AF_INET)
	{
		NetworkSocket* SocketImplementation = reinterpret_cast<NetworkSocket*>(SocketEntry->Implementation);
		if (SocketImplementation != nullptr && SocketImplementation->IsShutdownRead)
		{
			return 0;
		}
	}

	return LINUX_SOCKET_ERR_EBADF;
}

int64_t SocketWrite(File* OpenFile, const void* Buffer, uint64_t Count)
{
	(void) Buffer;
	(void) Count;

	Socket* SocketEntry = GetSocketFromOpenFile(OpenFile);
	if (SocketEntry == nullptr)
	{
		return LINUX_SOCKET_ERR_EBADF;
	}

	if (SocketEntry->Domain == LINUX_AF_UNIX)
	{
		UnixSocket* SocketImplementation = reinterpret_cast<UnixSocket*>(SocketEntry->Implementation);
		if (SocketImplementation != nullptr && SocketImplementation->IsShutdownWrite)
		{
			return LINUX_SOCKET_ERR_EPIPE;
		}
	}
	else if (SocketEntry->Domain == LINUX_AF_INET)
	{
		NetworkSocket* SocketImplementation = reinterpret_cast<NetworkSocket*>(SocketEntry->Implementation);
		if (SocketImplementation != nullptr && SocketImplementation->IsShutdownWrite)
		{
			return LINUX_SOCKET_ERR_EPIPE;
		}
	}

	return LINUX_SOCKET_ERR_EBADF;
}

int64_t SocketSeek(File* OpenFile, int64_t Offset, int32_t Whence)
{
	(void) OpenFile;
	(void) Offset;
	(void) Whence;
	return LINUX_SOCKET_ERR_EBADF;
}

int64_t SocketMemoryMap(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address)
{
	(void) OpenFile;
	(void) Length;
	(void) Offset;
	(void) AddressSpace;
	(void) Address;
	return LINUX_SOCKET_ERR_EBADF;
}

int64_t SocketPoll(File* OpenFile, uint32_t RequestedEvents, uint32_t* ReturnedEvents, LogicLayer* Logic, Process* RunningProcess)
{
	(void) OpenFile;
	(void) Logic;
	(void) RunningProcess;

	if (ReturnedEvents != nullptr)
	{
		*ReturnedEvents = RequestedEvents;
	}

	return 0;
}

int64_t SocketIoctl(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic, Process* RunningProcess)
{
	(void) OpenFile;
	(void) Request;
	(void) Argument;
	(void) Logic;
	(void) RunningProcess;
	return LINUX_SOCKET_ERR_EBADF;
}

PipeEndpoint* GetPipeEndpointFromOpenFile(File* OpenFile)
{
	if (OpenFile == nullptr || OpenFile->Node == nullptr || OpenFile->Node->NodeData == nullptr)
	{
		return nullptr;
	}

	return reinterpret_cast<PipeEndpoint*>(OpenFile->Node->NodeData);
}

int64_t PipeRead(File* OpenFile, void* Buffer, uint64_t Count)
{
	if ((Buffer == nullptr && Count != 0) || OpenFile == nullptr)
	{
		return LINUX_PIPE_ERR_EINVAL;
	}

	if (Count == 0)
	{
		return 0;
	}

	PipeEndpoint* Endpoint = GetPipeEndpointFromOpenFile(OpenFile);
	if (Endpoint == nullptr || Endpoint->Pipe == nullptr)
	{
		return LINUX_PIPE_ERR_EBADF;
	}

	if (!Endpoint->IsReadEnd)
	{
		return LINUX_PIPE_ERR_EBADF;
	}

	PipeKernelObject* Pipe = Endpoint->Pipe;
	if (Pipe->BufferedBytes == 0)
	{
		if (Pipe->WriterReferenceCount == 0)
		{
			return 0;
		}

		return LINUX_PIPE_ERR_EAGAIN;
	}

	uint64_t BytesToRead = (Count < Pipe->BufferedBytes) ? Count : Pipe->BufferedBytes;
	for (uint64_t Index = 0; Index < BytesToRead; ++Index)
	{
		reinterpret_cast<uint8_t*>(Buffer)[Index] = Pipe->Buffer[Pipe->ReadOffset];
		Pipe->ReadOffset = (Pipe->ReadOffset + 1) % LINUX_PIPE_BUFFER_CAPACITY;
	}

	Pipe->BufferedBytes -= BytesToRead;
	return static_cast<int64_t>(BytesToRead);
}

int64_t PipeWrite(File* OpenFile, const void* Buffer, uint64_t Count)
{
	if ((Buffer == nullptr && Count != 0) || OpenFile == nullptr)
	{
		return LINUX_PIPE_ERR_EINVAL;
	}

	if (Count == 0)
	{
		return 0;
	}

	PipeEndpoint* Endpoint = GetPipeEndpointFromOpenFile(OpenFile);
	if (Endpoint == nullptr || Endpoint->Pipe == nullptr)
	{
		return LINUX_PIPE_ERR_EBADF;
	}

	if (Endpoint->IsReadEnd)
	{
		return LINUX_PIPE_ERR_EBADF;
	}

	PipeKernelObject* Pipe = Endpoint->Pipe;
	if (Pipe->ReaderReferenceCount == 0)
	{
		return LINUX_PIPE_ERR_EPIPE;
	}

	uint64_t AvailableSpace = LINUX_PIPE_BUFFER_CAPACITY - Pipe->BufferedBytes;
	if (AvailableSpace == 0)
	{
		return LINUX_PIPE_ERR_EAGAIN;
	}

	uint64_t BytesToWrite = (Count < AvailableSpace) ? Count : AvailableSpace;
	for (uint64_t Index = 0; Index < BytesToWrite; ++Index)
	{
		Pipe->Buffer[Pipe->WriteOffset] = reinterpret_cast<const uint8_t*>(Buffer)[Index];
		Pipe->WriteOffset = (Pipe->WriteOffset + 1) % LINUX_PIPE_BUFFER_CAPACITY;
	}

	Pipe->BufferedBytes += BytesToWrite;
	return static_cast<int64_t>(BytesToWrite);
}

int64_t PipePoll(File* OpenFile, uint32_t RequestedEvents, uint32_t* ReturnedEvents, LogicLayer* Logic, Process* RunningProcess)
{
	(void) Logic;
	(void) RunningProcess;

	if (ReturnedEvents == nullptr)
	{
		return 0;
	}

	*ReturnedEvents = 0;

	PipeEndpoint* Endpoint = GetPipeEndpointFromOpenFile(OpenFile);
	if (Endpoint == nullptr || Endpoint->Pipe == nullptr)
	{
		*ReturnedEvents = LINUX_POLLERR;
		return 0;
	}

	PipeKernelObject* Pipe = Endpoint->Pipe;

	if (Endpoint->IsReadEnd)
	{
		if ((RequestedEvents & LINUX_POLLIN) != 0)
		{
			if (Pipe->BufferedBytes > 0)
			{
				*ReturnedEvents |= LINUX_POLLIN;
			}
			else if (Pipe->WriterReferenceCount == 0)
			{
				*ReturnedEvents |= LINUX_POLLHUP;
			}
		}
	}
	else
	{
		if ((RequestedEvents & LINUX_POLLOUT) != 0)
		{
			if (Pipe->ReaderReferenceCount == 0)
			{
				*ReturnedEvents |= (LINUX_POLLERR | LINUX_POLLHUP);
			}
			else if (Pipe->BufferedBytes < LINUX_PIPE_BUFFER_CAPACITY)
			{
				*ReturnedEvents |= LINUX_POLLOUT;
			}
		}
	}

	return 0;
}

int64_t PipeIoctl(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic, Process* RunningProcess)
{
	(void) OpenFile;
	(void) Request;
	(void) Argument;
	(void) Logic;
	(void) RunningProcess;
	return -25;
}

FileOperations PipeFileOperations = {
	PipeRead,
	PipeWrite,
	nullptr,
	nullptr,
	PipePoll,
	PipeIoctl};
} // namespace

/**
 * Function: InterProcessComunicationManager::InterProcessComunicationManager
 * Description: Constructs inter-process communication manager.
 * Parameters:
 *   None
 * Returns:
 *   InterProcessComunicationManager - Constructed manager instance.
 */
InterProcessComunicationManager::InterProcessComunicationManager()
	: SocketCount(0), PipeEndpointCount(0)
{
	for (uint64_t Index = 0; Index < MAX_TRACKED_SOCKETS; ++Index)
	{
		SocketList[Index] = nullptr;
	}

	for (uint64_t Index = 0; Index < MAX_TRACKED_PIPES; ++Index)
	{
		PipeEndpointList[Index].Owner          = nullptr;
		PipeEndpointList[Index].FileDescriptor = -1;
		PipeEndpointList[Index].Node           = nullptr;
		PipeEndpointList[Index].Endpoint       = nullptr;
	}
}

/**
 * Function: InterProcessComunicationManager::~InterProcessComunicationManager
 * Description: Destroys inter-process communication manager.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
InterProcessComunicationManager::~InterProcessComunicationManager()
{
	while (PipeEndpointCount > 0)
	{
		TrackedPipeEndpoint Entry = PipeEndpointList[0];
		if (!ClosePipe(Entry.Owner, Entry.FileDescriptor, Entry.Node))
		{
			for (uint64_t ShiftIndex = 1; ShiftIndex < PipeEndpointCount; ++ShiftIndex)
			{
				PipeEndpointList[ShiftIndex - 1] = PipeEndpointList[ShiftIndex];
			}

			--PipeEndpointCount;
			PipeEndpointList[PipeEndpointCount].Owner          = nullptr;
			PipeEndpointList[PipeEndpointCount].FileDescriptor = -1;
			PipeEndpointList[PipeEndpointCount].Node           = nullptr;
			PipeEndpointList[PipeEndpointCount].Endpoint       = nullptr;
		}
	}

	while (SocketCount > 0)
	{
		Socket* SocketEntry = SocketList[SocketCount - 1];
		SocketList[SocketCount - 1] = nullptr;
		--SocketCount;
		DestroySocket(SocketEntry);
	}
}

void InterProcessComunicationManager::InitializeSocketFileOperations(Socket* SocketEntry)
{
	if (SocketEntry == nullptr)
	{
		return;
	}

	SocketEntry->FileOps.Read      = SocketRead;
	SocketEntry->FileOps.Write     = SocketWrite;
	SocketEntry->FileOps.Seek      = SocketSeek;
	SocketEntry->FileOps.MemoryMap = SocketMemoryMap;
	SocketEntry->FileOps.Poll      = SocketPoll;
	SocketEntry->FileOps.Ioctl     = SocketIoctl;
}

void InterProcessComunicationManager::DestroySocket(Socket* SocketEntry)
{
	if (SocketEntry == nullptr)
	{
		return;
	}

	if (SocketEntry->Domain == LINUX_AF_UNIX)
	{
		UnixSocket* SocketImplementation = reinterpret_cast<UnixSocket*>(SocketEntry->Implementation);
		if (SocketImplementation != nullptr && SocketImplementation->Path != nullptr)
		{
			delete[] SocketImplementation->Path;
			SocketImplementation->Path = nullptr;
		}

		if (SocketImplementation != nullptr && SocketImplementation->PendingConnections != nullptr)
		{
			delete[] SocketImplementation->PendingConnections;
			SocketImplementation->PendingConnections = nullptr;
			SocketImplementation->PendingConnectionCount = 0;
			SocketImplementation->PendingConnectionCapacity = 0;
		}

		delete SocketImplementation;
	}
	else if (SocketEntry->Domain == LINUX_AF_INET)
	{
		delete reinterpret_cast<NetworkSocket*>(SocketEntry->Implementation);
	}
	else if (SocketEntry->Domain == LINUX_AF_NETLINK)
	{
		delete reinterpret_cast<NetlinkSocket*>(SocketEntry->Implementation);
	}

	delete SocketEntry;
}

Socket* InterProcessComunicationManager::CreateSocket(int64_t Domain, int64_t Type, int64_t Protocol, Process* Owner, int64_t FileDescriptor, int64_t* ErrorCode)
{
	if (ErrorCode != nullptr)
	{
		*ErrorCode = 0;
	}

	if (Owner == nullptr || FileDescriptor < 0)
	{
		if (ErrorCode != nullptr)
		{
			*ErrorCode = LINUX_SOCKET_ERR_EINVAL;
		}
		return nullptr;
	}

	if (SocketCount >= MAX_TRACKED_SOCKETS)
	{
		if (ErrorCode != nullptr)
		{
			*ErrorCode = LINUX_SOCKET_ERR_EMFILE;
		}
		return nullptr;
	}

	if (Domain != LINUX_AF_UNIX && Domain != LINUX_AF_INET && Domain != LINUX_AF_NETLINK)
	{
		if (ErrorCode != nullptr)
		{
			*ErrorCode = LINUX_SOCKET_ERR_EAFNOSUPPORT;
		}
		return nullptr;
	}

	int64_t SocketType = (Type & LINUX_SOCK_TYPE_MASK);
	if (SocketType != LINUX_SOCK_STREAM && SocketType != LINUX_SOCK_DGRAM && SocketType != LINUX_SOCK_RAW && SocketType != LINUX_SOCK_SEQPACKET)
	{
		if (ErrorCode != nullptr)
		{
			*ErrorCode = LINUX_SOCKET_ERR_ESOCKTNOSUPPORT;
		}
		return nullptr;
	}

	Socket* NewSocket = new Socket;
	if (NewSocket == nullptr)
	{
		if (ErrorCode != nullptr)
		{
			*ErrorCode = LINUX_SOCKET_ERR_ENOMEM;
		}
		return nullptr;
	}

	NewSocket->Domain         = static_cast<int32_t>(Domain);
	NewSocket->Type           = static_cast<int32_t>(SocketType);
	NewSocket->Protocol       = static_cast<int32_t>(Protocol);
	NewSocket->Owner          = Owner;
	NewSocket->FileDescriptor = FileDescriptor;
	NewSocket->Implementation = nullptr;
	InitializeSocketFileOperations(NewSocket);

	if (Domain == LINUX_AF_UNIX)
	{
		UnixSocket* SocketImplementation = new UnixSocket;
		if (SocketImplementation == nullptr)
		{
			delete NewSocket;
			if (ErrorCode != nullptr)
			{
				*ErrorCode = LINUX_SOCKET_ERR_ENOMEM;
			}
			return nullptr;
		}

		SocketImplementation->Path = nullptr;
		SocketImplementation->PathLength = 0;
		SocketImplementation->IsAbstract = false;
		SocketImplementation->IsBound = false;
		SocketImplementation->IsListening = false;
		SocketImplementation->ListenBacklog = 0;
		SocketImplementation->PendingConnections = nullptr;
		SocketImplementation->PendingConnectionCount = 0;
		SocketImplementation->PendingConnectionCapacity = 0;
		SocketImplementation->ConnectedPeer = nullptr;
		SocketImplementation->IsShutdownRead = false;
		SocketImplementation->IsShutdownWrite = false;
		NewSocket->Implementation  = SocketImplementation;
	}
	else if (Domain == LINUX_AF_INET)
	{
		NetworkSocket* SocketImplementation = new NetworkSocket;
		if (SocketImplementation == nullptr)
		{
			delete NewSocket;
			if (ErrorCode != nullptr)
			{
				*ErrorCode = LINUX_SOCKET_ERR_ENOMEM;
			}
			return nullptr;
		}

		SocketImplementation->LocalIp    = 0;
		SocketImplementation->LocalPort  = 0;
		SocketImplementation->RemoteIp   = 0;
		SocketImplementation->RemotePort = 0;
		SocketImplementation->IsBound    = false;
		SocketImplementation->IsListening = false;
		SocketImplementation->ListenBacklog = 0;
		SocketImplementation->IsShutdownRead = false;
		SocketImplementation->IsShutdownWrite = false;
		NewSocket->Implementation        = SocketImplementation;
	}
	else
	{
		if (SocketType != LINUX_SOCK_DGRAM && SocketType != LINUX_SOCK_RAW)
		{
			delete NewSocket;
			if (ErrorCode != nullptr)
			{
				*ErrorCode = LINUX_SOCKET_ERR_ESOCKTNOSUPPORT;
			}
			return nullptr;
		}

		NetlinkSocket* SocketImplementation = new NetlinkSocket;
		if (SocketImplementation == nullptr)
		{
			delete NewSocket;
			if (ErrorCode != nullptr)
			{
				*ErrorCode = LINUX_SOCKET_ERR_ENOMEM;
			}
			return nullptr;
		}

		SocketImplementation->LocalPid = 0;
		SocketImplementation->LocalGroups = 0;
		SocketImplementation->RemotePid = 0;
		SocketImplementation->RemoteGroups = 0;
		SocketImplementation->IsBound = false;
		SocketImplementation->IsShutdownRead = false;
		SocketImplementation->IsShutdownWrite = false;
		NewSocket->Implementation = SocketImplementation;
	}

	SocketList[SocketCount++] = NewSocket;
	return NewSocket;
}

int64_t InterProcessComunicationManager::BindSocket(Process* Owner, int64_t FileDescriptor, const void* SocketAddress, uint64_t SocketAddressLength)
{
	if (Owner == nullptr || FileDescriptor < 0 || SocketAddress == nullptr)
	{
		return LINUX_SOCKET_ERR_EINVAL;
	}

	if (SocketAddressLength < LINUX_SOCKADDR_FAMILY_SIZE)
	{
		return LINUX_SOCKET_ERR_EINVAL;
	}

	Socket* SocketEntry = nullptr;
	for (uint64_t SocketIndex = 0; SocketIndex < SocketCount; ++SocketIndex)
	{
		Socket* Candidate = SocketList[SocketIndex];
		if (Candidate == nullptr)
		{
			continue;
		}

		if (Candidate->Owner == Owner && Candidate->FileDescriptor == FileDescriptor)
		{
			SocketEntry = Candidate;
			break;
		}
	}

	if (SocketEntry == nullptr)
	{
		return LINUX_SOCKET_ERR_ENOTSOCK;
	}

	uint16_t AddressFamily = ReadLinuxAddressFamily(SocketAddress);
	if (AddressFamily != static_cast<uint16_t>(SocketEntry->Domain))
	{
		return LINUX_SOCKET_ERR_EAFNOSUPPORT;
	}

	if (SocketEntry->Domain == LINUX_AF_INET)
	{
		if (SocketAddressLength < sizeof(LinuxSockAddrIn))
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		const LinuxSockAddrIn* InternetAddress = reinterpret_cast<const LinuxSockAddrIn*>(SocketAddress);

		for (uint64_t SocketIndex = 0; SocketIndex < SocketCount; ++SocketIndex)
		{
			Socket* CandidateEntry = SocketList[SocketIndex];
			if (CandidateEntry == nullptr || CandidateEntry == SocketEntry || CandidateEntry->Domain != LINUX_AF_INET)
			{
				continue;
			}

			NetworkSocket* CandidateNetwork = reinterpret_cast<NetworkSocket*>(CandidateEntry->Implementation);
			if (CandidateNetwork == nullptr || !CandidateNetwork->IsBound)
			{
				continue;
			}

			if (CandidateNetwork->LocalPort != InternetAddress->Port)
			{
				continue;
			}

			if (CandidateNetwork->LocalIp == 0 || InternetAddress->Address == 0 || CandidateNetwork->LocalIp == InternetAddress->Address)
			{
				return LINUX_SOCKET_ERR_EADDRINUSE;
			}
		}

		NetworkSocket* SocketImplementation = reinterpret_cast<NetworkSocket*>(SocketEntry->Implementation);
		if (SocketImplementation == nullptr)
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		SocketImplementation->LocalIp   = InternetAddress->Address;
		SocketImplementation->LocalPort = InternetAddress->Port;
		SocketImplementation->IsBound   = true;
		return 0;
	}

	if (SocketEntry->Domain == LINUX_AF_UNIX)
	{
		if (SocketAddressLength <= LINUX_SOCKADDR_FAMILY_SIZE)
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		if (SocketAddressLength > sizeof(LinuxSockAddrUn))
		{
			return LINUX_SOCKET_ERR_ENAMETOOLONG;
		}

		const LinuxSockAddrUn* UnixAddress      = reinterpret_cast<const LinuxSockAddrUn*>(SocketAddress);
		uint64_t               AvailablePathLen = SocketAddressLength - LINUX_SOCKADDR_FAMILY_SIZE;
		bool                   IsAbstractAddress = false;
		uint64_t               PathLength = ComputeUnixAddressPathLength(UnixAddress->Path, AvailablePathLen, &IsAbstractAddress);

		if (PathLength == 0)
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		for (uint64_t SocketIndex = 0; SocketIndex < SocketCount; ++SocketIndex)
		{
			Socket* CandidateEntry = SocketList[SocketIndex];
			if (CandidateEntry == nullptr || CandidateEntry == SocketEntry || CandidateEntry->Domain != LINUX_AF_UNIX)
			{
				continue;
			}

			UnixSocket* CandidateUnix = reinterpret_cast<UnixSocket*>(CandidateEntry->Implementation);
			if (CandidateUnix == nullptr || !CandidateUnix->IsBound || CandidateUnix->Path == nullptr)
			{
				continue;
			}

			if (CandidateUnix->IsAbstract != IsAbstractAddress)
			{
				continue;
			}

			if (AreUnixPathsEqual(CandidateUnix->Path, CandidateUnix->PathLength, UnixAddress->Path, PathLength, IsAbstractAddress))
			{
				return LINUX_SOCKET_ERR_EADDRINUSE;
			}
		}

		UnixSocket* SocketImplementation = reinterpret_cast<UnixSocket*>(SocketEntry->Implementation);
		if (SocketImplementation == nullptr)
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		char* NewPath = new char[PathLength + 1];
		if (NewPath == nullptr)
		{
			return LINUX_SOCKET_ERR_ENOMEM;
		}

		for (uint64_t PathIndex = 0; PathIndex < PathLength; ++PathIndex)
		{
			NewPath[PathIndex] = UnixAddress->Path[PathIndex];
		}

		NewPath[PathLength] = '\0';

		if (SocketImplementation->Path != nullptr)
		{
			delete[] SocketImplementation->Path;
		}

		SocketImplementation->Path       = NewPath;
		SocketImplementation->PathLength = PathLength;
		SocketImplementation->IsAbstract = IsAbstractAddress;
		SocketImplementation->IsBound    = true;
		return 0;
	}

	if (SocketEntry->Domain == LINUX_AF_NETLINK)
	{
		if (SocketAddressLength < sizeof(LinuxSockAddrNl))
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		const LinuxSockAddrNl* NetlinkAddress = reinterpret_cast<const LinuxSockAddrNl*>(SocketAddress);

		for (uint64_t SocketIndex = 0; SocketIndex < SocketCount; ++SocketIndex)
		{
			Socket* CandidateEntry = SocketList[SocketIndex];
			if (CandidateEntry == nullptr || CandidateEntry == SocketEntry || CandidateEntry->Domain != LINUX_AF_NETLINK)
			{
				continue;
			}

			NetlinkSocket* CandidateNetlink = reinterpret_cast<NetlinkSocket*>(CandidateEntry->Implementation);
			if (CandidateNetlink == nullptr || !CandidateNetlink->IsBound)
			{
				continue;
			}

			if (NetlinkAddress->Pid != 0 && CandidateNetlink->LocalPid == NetlinkAddress->Pid)
			{
				return LINUX_SOCKET_ERR_EADDRINUSE;
			}
		}

		NetlinkSocket* SocketImplementation = reinterpret_cast<NetlinkSocket*>(SocketEntry->Implementation);
		if (SocketImplementation == nullptr)
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		SocketImplementation->LocalPid = NetlinkAddress->Pid;
		SocketImplementation->LocalGroups = NetlinkAddress->Groups;
		SocketImplementation->IsBound = true;
		return 0;
	}

	return LINUX_SOCKET_ERR_EAFNOSUPPORT;
}

int64_t InterProcessComunicationManager::ListenSocket(Process* Owner, int64_t FileDescriptor, int64_t Backlog)
{
	if (Owner == nullptr || FileDescriptor < 0)
	{
		return LINUX_SOCKET_ERR_EINVAL;
	}

	Socket* SocketEntry = nullptr;
	for (uint64_t SocketIndex = 0; SocketIndex < SocketCount; ++SocketIndex)
	{
		Socket* Candidate = SocketList[SocketIndex];
		if (Candidate == nullptr)
		{
			continue;
		}

		if (Candidate->Owner == Owner && Candidate->FileDescriptor == FileDescriptor)
		{
			SocketEntry = Candidate;
			break;
		}
	}

	if (SocketEntry == nullptr)
	{
		return LINUX_SOCKET_ERR_ENOTSOCK;
	}

	if (SocketEntry->Type != LINUX_SOCK_STREAM && SocketEntry->Type != LINUX_SOCK_SEQPACKET)
	{
		return LINUX_SOCKET_ERR_EOPNOTSUPP;
	}

	if (Backlog < 0)
	{
		Backlog = 0;
	}

	if (Backlog > LINUX_LISTEN_SOMAXCONN)
	{
		Backlog = LINUX_LISTEN_SOMAXCONN;
	}

	if (SocketEntry->Domain == LINUX_AF_INET)
	{
		NetworkSocket* SocketImplementation = reinterpret_cast<NetworkSocket*>(SocketEntry->Implementation);
		if (SocketImplementation == nullptr)
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		if (!SocketImplementation->IsBound)
		{
			bool PortFound = false;
			for (uint32_t CandidatePort = LINUX_INET_EPHEMERAL_START; CandidatePort <= LINUX_INET_EPHEMERAL_END; ++CandidatePort)
			{
				bool PortInUse = false;
				for (uint64_t SocketIndex = 0; SocketIndex < SocketCount; ++SocketIndex)
				{
					Socket* CandidateEntry = SocketList[SocketIndex];
					if (CandidateEntry == nullptr || CandidateEntry == SocketEntry || CandidateEntry->Domain != LINUX_AF_INET)
					{
						continue;
					}

					NetworkSocket* CandidateNetwork = reinterpret_cast<NetworkSocket*>(CandidateEntry->Implementation);
					if (CandidateNetwork == nullptr || !CandidateNetwork->IsBound)
					{
						continue;
					}

					if (CandidateNetwork->LocalPort == static_cast<uint16_t>(CandidatePort))
					{
						PortInUse = true;
						break;
					}
				}

				if (PortInUse)
				{
					continue;
				}

				SocketImplementation->LocalIp = 0;
				SocketImplementation->LocalPort = static_cast<uint16_t>(CandidatePort);
				SocketImplementation->IsBound = true;
				PortFound = true;
				break;
			}

			if (!PortFound)
			{
				return LINUX_SOCKET_ERR_EADDRINUSE;
			}
		}

		SocketImplementation->IsListening = true;
		SocketImplementation->ListenBacklog = static_cast<int32_t>(Backlog);
		return 0;
	}

	if (SocketEntry->Domain == LINUX_AF_UNIX)
	{
		UnixSocket* SocketImplementation = reinterpret_cast<UnixSocket*>(SocketEntry->Implementation);
		if (SocketImplementation == nullptr)
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		if (!SocketImplementation->IsBound)
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		uint64_t RequestedCapacity = static_cast<uint64_t>(Backlog);
		if (RequestedCapacity != SocketImplementation->PendingConnectionCapacity)
		{
			Socket** NewPendingConnections = nullptr;
			if (RequestedCapacity > 0)
			{
				NewPendingConnections = new Socket*[RequestedCapacity];
				if (NewPendingConnections == nullptr)
				{
					return LINUX_SOCKET_ERR_ENOMEM;
				}
			}

			uint64_t ElementsToCopy = SocketImplementation->PendingConnectionCount;
			if (ElementsToCopy > RequestedCapacity)
			{
				ElementsToCopy = RequestedCapacity;
			}

			for (uint64_t Index = 0; Index < ElementsToCopy; ++Index)
			{
				NewPendingConnections[Index] = SocketImplementation->PendingConnections[Index];
			}

			delete[] SocketImplementation->PendingConnections;
			SocketImplementation->PendingConnections = NewPendingConnections;
			SocketImplementation->PendingConnectionCapacity = RequestedCapacity;
			SocketImplementation->PendingConnectionCount = ElementsToCopy;
		}

		SocketImplementation->IsListening = true;
		SocketImplementation->ListenBacklog = static_cast<int32_t>(Backlog);
		return 0;
	}

	if (SocketEntry->Domain == LINUX_AF_NETLINK)
	{
		return LINUX_SOCKET_ERR_EOPNOTSUPP;
	}

	return LINUX_SOCKET_ERR_EAFNOSUPPORT;
}

int64_t InterProcessComunicationManager::ConnectSocket(Process* Owner, int64_t FileDescriptor, const void* SocketAddress, uint64_t SocketAddressLength)
{
	if (Owner == nullptr || FileDescriptor < 0 || SocketAddress == nullptr)
	{
		return LINUX_SOCKET_ERR_EINVAL;
	}

	if (SocketAddressLength < LINUX_SOCKADDR_FAMILY_SIZE)
	{
		return LINUX_SOCKET_ERR_EINVAL;
	}

	Socket* ClientSocket = nullptr;
	for (uint64_t SocketIndex = 0; SocketIndex < SocketCount; ++SocketIndex)
	{
		Socket* Candidate = SocketList[SocketIndex];
		if (Candidate == nullptr)
		{
			continue;
		}

		if (Candidate->Owner == Owner && Candidate->FileDescriptor == FileDescriptor)
		{
			ClientSocket = Candidate;
			break;
		}
	}

	if (ClientSocket == nullptr)
	{
		return LINUX_SOCKET_ERR_ENOTSOCK;
	}

	if (ClientSocket->Domain != LINUX_AF_NETLINK && ClientSocket->Type != LINUX_SOCK_STREAM && ClientSocket->Type != LINUX_SOCK_SEQPACKET)
	{
		return LINUX_SOCKET_ERR_EOPNOTSUPP;
	}

	uint16_t AddressFamily = ReadLinuxAddressFamily(SocketAddress);
	if (AddressFamily != static_cast<uint16_t>(ClientSocket->Domain))
	{
		return LINUX_SOCKET_ERR_EAFNOSUPPORT;
	}

	if (ClientSocket->Domain == LINUX_AF_NETLINK)
	{
		if (SocketAddressLength < sizeof(LinuxSockAddrNl))
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		NetlinkSocket* ClientNetlink = reinterpret_cast<NetlinkSocket*>(ClientSocket->Implementation);
		if (ClientNetlink == nullptr)
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		const LinuxSockAddrNl* NetlinkAddress = reinterpret_cast<const LinuxSockAddrNl*>(SocketAddress);
		ClientNetlink->RemotePid = NetlinkAddress->Pid;
		ClientNetlink->RemoteGroups = NetlinkAddress->Groups;
		return 0;
	}

	if (ClientSocket->Domain != LINUX_AF_UNIX)
	{
		return LINUX_SOCKET_ERR_EAFNOSUPPORT;
	}

	if (SocketAddressLength <= LINUX_SOCKADDR_FAMILY_SIZE)
	{
		return LINUX_SOCKET_ERR_EINVAL;
	}

	if (SocketAddressLength > sizeof(LinuxSockAddrUn))
	{
		return LINUX_SOCKET_ERR_ENAMETOOLONG;
	}

	UnixSocket* ClientUnix = reinterpret_cast<UnixSocket*>(ClientSocket->Implementation);
	if (ClientUnix == nullptr)
	{
		return LINUX_SOCKET_ERR_EINVAL;
	}

	if (ClientUnix->ConnectedPeer != nullptr)
	{
		return LINUX_SOCKET_ERR_EISCONN;
	}

	const LinuxSockAddrUn* UnixAddress = reinterpret_cast<const LinuxSockAddrUn*>(SocketAddress);
	uint64_t               AvailablePathLen = SocketAddressLength - LINUX_SOCKADDR_FAMILY_SIZE;
	bool                   IsAbstractAddress = false;
	uint64_t               PathLength = ComputeUnixAddressPathLength(UnixAddress->Path, AvailablePathLen, &IsAbstractAddress);
	if (PathLength == 0)
	{
		return LINUX_SOCKET_ERR_EINVAL;
	}

	Socket* ListeningSocket = nullptr;
	UnixSocket* ListeningUnix = nullptr;
	for (uint64_t SocketIndex = 0; SocketIndex < SocketCount; ++SocketIndex)
	{
		Socket* Candidate = SocketList[SocketIndex];
		if (Candidate == nullptr || Candidate->Domain != LINUX_AF_UNIX)
		{
			continue;
		}

		UnixSocket* CandidateUnix = reinterpret_cast<UnixSocket*>(Candidate->Implementation);
		if (CandidateUnix == nullptr || !CandidateUnix->IsBound || !CandidateUnix->IsListening || CandidateUnix->Path == nullptr)
		{
			continue;
		}

		if (CandidateUnix->IsAbstract != IsAbstractAddress)
		{
			continue;
		}

		if (AreUnixPathsEqual(CandidateUnix->Path, CandidateUnix->PathLength, UnixAddress->Path, PathLength, IsAbstractAddress))
		{
			ListeningSocket = Candidate;
			ListeningUnix = CandidateUnix;
			break;
		}
	}

	if (ListeningSocket == nullptr || ListeningUnix == nullptr)
	{
		return LINUX_SOCKET_ERR_ECONNREFUSED;
	}

	if (ListeningUnix->PendingConnectionCount >= ListeningUnix->PendingConnectionCapacity)
	{
		return LINUX_SOCKET_ERR_EAGAIN;
	}

	ListeningUnix->PendingConnections[ListeningUnix->PendingConnectionCount++] = ClientSocket;
	ClientUnix->ConnectedPeer = ListeningSocket;
	return 0;
}

Socket* InterProcessComunicationManager::AcceptSocket(Process* Owner, int64_t FileDescriptor, Process* AcceptedOwner, int64_t AcceptedFileDescriptor, int64_t* ErrorCode)
{
	if (ErrorCode != nullptr)
	{
		*ErrorCode = 0;
	}

	if (Owner == nullptr || AcceptedOwner == nullptr || FileDescriptor < 0 || AcceptedFileDescriptor < 0)
	{
		if (ErrorCode != nullptr)
		{
			*ErrorCode = LINUX_SOCKET_ERR_EINVAL;
		}
		return nullptr;
	}

	Socket* ListeningSocket = nullptr;
	for (uint64_t SocketIndex = 0; SocketIndex < SocketCount; ++SocketIndex)
	{
		Socket* Candidate = SocketList[SocketIndex];
		if (Candidate == nullptr)
		{
			continue;
		}

		if (Candidate->Owner == Owner && Candidate->FileDescriptor == FileDescriptor)
		{
			ListeningSocket = Candidate;
			break;
		}
	}

	if (ListeningSocket == nullptr)
	{
		if (ErrorCode != nullptr)
		{
			*ErrorCode = LINUX_SOCKET_ERR_ENOTSOCK;
		}
		return nullptr;
	}

	if (ListeningSocket->Domain != LINUX_AF_UNIX)
	{
		if (ErrorCode != nullptr)
		{
			*ErrorCode = LINUX_SOCKET_ERR_EAFNOSUPPORT;
		}
		return nullptr;
	}

	UnixSocket* ListeningUnix = reinterpret_cast<UnixSocket*>(ListeningSocket->Implementation);
	if (ListeningUnix == nullptr || !ListeningUnix->IsListening)
	{
		if (ErrorCode != nullptr)
		{
			*ErrorCode = LINUX_SOCKET_ERR_EINVAL;
		}
		return nullptr;
	}

	Socket* PendingClient = nullptr;
	while (ListeningUnix->PendingConnectionCount > 0)
	{
		PendingClient = ListeningUnix->PendingConnections[0];

		for (uint64_t ShiftIndex = 1; ShiftIndex < ListeningUnix->PendingConnectionCount; ++ShiftIndex)
		{
			ListeningUnix->PendingConnections[ShiftIndex - 1] = ListeningUnix->PendingConnections[ShiftIndex];
		}

		--ListeningUnix->PendingConnectionCount;

		if (IsSocketTracked(SocketList, SocketCount, PendingClient))
		{
			break;
		}

		PendingClient = nullptr;
	}

	if (PendingClient == nullptr)
	{
		if (ErrorCode != nullptr)
		{
			*ErrorCode = LINUX_SOCKET_ERR_EAGAIN;
		}
		return nullptr;
	}

	int64_t CreateError = 0;
	Socket* AcceptedSocket = CreateSocket(ListeningSocket->Domain, ListeningSocket->Type, ListeningSocket->Protocol, AcceptedOwner, AcceptedFileDescriptor, &CreateError);
	if (AcceptedSocket == nullptr)
	{
		if (ErrorCode != nullptr)
		{
			*ErrorCode = (CreateError != 0) ? CreateError : static_cast<int64_t>(LINUX_SOCKET_ERR_ENOMEM);
		}
		return nullptr;
	}

	UnixSocket* AcceptedUnix = reinterpret_cast<UnixSocket*>(AcceptedSocket->Implementation);
	UnixSocket* ClientUnix = reinterpret_cast<UnixSocket*>(PendingClient->Implementation);
	if (AcceptedUnix == nullptr || ClientUnix == nullptr)
	{
		CloseSocket(AcceptedOwner, AcceptedFileDescriptor);
		if (ErrorCode != nullptr)
		{
			*ErrorCode = LINUX_SOCKET_ERR_EINVAL;
		}
		return nullptr;
	}

	AcceptedUnix->ConnectedPeer = PendingClient;
	ClientUnix->ConnectedPeer = AcceptedSocket;
	return AcceptedSocket;
}

int64_t InterProcessComunicationManager::ShutdownSocket(Process* Owner, int64_t FileDescriptor, int64_t How)
{
	if (Owner == nullptr || FileDescriptor < 0)
	{
		return LINUX_SOCKET_ERR_EINVAL;
	}

	if (How != LINUX_SHUT_RD && How != LINUX_SHUT_WR && How != LINUX_SHUT_RDWR)
	{
		return LINUX_SOCKET_ERR_EINVAL;
	}

	Socket* SocketEntry = nullptr;
	for (uint64_t SocketIndex = 0; SocketIndex < SocketCount; ++SocketIndex)
	{
		Socket* Candidate = SocketList[SocketIndex];
		if (Candidate == nullptr)
		{
			continue;
		}

		if (Candidate->Owner == Owner && Candidate->FileDescriptor == FileDescriptor)
		{
			SocketEntry = Candidate;
			break;
		}
	}

	if (SocketEntry == nullptr)
	{
		return LINUX_SOCKET_ERR_ENOTSOCK;
	}

	bool IsConnected = false;

	if (SocketEntry->Domain == LINUX_AF_UNIX)
	{
		UnixSocket* SocketImplementation = reinterpret_cast<UnixSocket*>(SocketEntry->Implementation);
		if (SocketImplementation == nullptr)
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		IsConnected = (SocketImplementation->ConnectedPeer != nullptr);
		if ((SocketEntry->Type == LINUX_SOCK_STREAM || SocketEntry->Type == LINUX_SOCK_SEQPACKET) && !IsConnected)
		{
			return LINUX_SOCKET_ERR_ENOTCONN;
		}

		if (How == LINUX_SHUT_RD || How == LINUX_SHUT_RDWR)
		{
			SocketImplementation->IsShutdownRead = true;
		}

		if (How == LINUX_SHUT_WR || How == LINUX_SHUT_RDWR)
		{
			SocketImplementation->IsShutdownWrite = true;
		}

		return 0;
	}

	if (SocketEntry->Domain == LINUX_AF_INET)
	{
		NetworkSocket* SocketImplementation = reinterpret_cast<NetworkSocket*>(SocketEntry->Implementation);
		if (SocketImplementation == nullptr)
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		IsConnected = (SocketImplementation->RemoteIp != 0 || SocketImplementation->RemotePort != 0);
		if ((SocketEntry->Type == LINUX_SOCK_STREAM || SocketEntry->Type == LINUX_SOCK_SEQPACKET) && !IsConnected)
		{
			return LINUX_SOCKET_ERR_ENOTCONN;
		}

		if (How == LINUX_SHUT_RD || How == LINUX_SHUT_RDWR)
		{
			SocketImplementation->IsShutdownRead = true;
		}

		if (How == LINUX_SHUT_WR || How == LINUX_SHUT_RDWR)
		{
			SocketImplementation->IsShutdownWrite = true;
		}

		return 0;
	}

	if (SocketEntry->Domain == LINUX_AF_NETLINK)
	{
		NetlinkSocket* SocketImplementation = reinterpret_cast<NetlinkSocket*>(SocketEntry->Implementation);
		if (SocketImplementation == nullptr)
		{
			return LINUX_SOCKET_ERR_EINVAL;
		}

		if (How == LINUX_SHUT_RD || How == LINUX_SHUT_RDWR)
		{
			SocketImplementation->IsShutdownRead = true;
		}

		if (How == LINUX_SHUT_WR || How == LINUX_SHUT_RDWR)
		{
			SocketImplementation->IsShutdownWrite = true;
		}

		return 0;
	}

	return LINUX_SOCKET_ERR_EAFNOSUPPORT;
}

bool InterProcessComunicationManager::CloseSocket(Process* Owner, int64_t FileDescriptor)
{
	if (Owner == nullptr || FileDescriptor < 0)
	{
		return false;
	}

	for (uint64_t SocketIndex = 0; SocketIndex < SocketCount; ++SocketIndex)
	{
		Socket* SocketEntry = SocketList[SocketIndex];
		if (SocketEntry == nullptr)
		{
			continue;
		}

		if (SocketEntry->Owner != Owner || SocketEntry->FileDescriptor != FileDescriptor)
		{
			continue;
		}

		for (uint64_t OtherIndex = 0; OtherIndex < SocketCount; ++OtherIndex)
		{
			Socket* OtherSocket = SocketList[OtherIndex];
			if (OtherSocket == nullptr || OtherSocket == SocketEntry || OtherSocket->Domain != LINUX_AF_UNIX)
			{
				continue;
			}

			UnixSocket* OtherUnix = reinterpret_cast<UnixSocket*>(OtherSocket->Implementation);
			if (OtherUnix == nullptr)
			{
				continue;
			}

			if (OtherUnix->ConnectedPeer == SocketEntry)
			{
				OtherUnix->ConnectedPeer = nullptr;
			}

			RemovePendingConnectionFromUnixQueue(OtherUnix, SocketEntry);
		}

		if (SocketEntry->Domain == LINUX_AF_UNIX)
		{
			UnixSocket* ClosingUnix = reinterpret_cast<UnixSocket*>(SocketEntry->Implementation);
			if (ClosingUnix != nullptr)
			{
				for (uint64_t QueueIndex = 0; QueueIndex < ClosingUnix->PendingConnectionCount; ++QueueIndex)
				{
					Socket* PendingSocket = ClosingUnix->PendingConnections[QueueIndex];
					if (PendingSocket == nullptr || PendingSocket->Domain != LINUX_AF_UNIX)
					{
						continue;
					}

					UnixSocket* PendingUnix = reinterpret_cast<UnixSocket*>(PendingSocket->Implementation);
					if (PendingUnix != nullptr && PendingUnix->ConnectedPeer == SocketEntry)
					{
						PendingUnix->ConnectedPeer = nullptr;
					}
				}
			}
		}

		DestroySocket(SocketEntry);

		for (uint64_t ShiftIndex = SocketIndex + 1; ShiftIndex < SocketCount; ++ShiftIndex)
		{
			SocketList[ShiftIndex - 1] = SocketList[ShiftIndex];
		}

		--SocketCount;
		SocketList[SocketCount] = nullptr;
		return true;
	}

	return false;
}

int64_t InterProcessComunicationManager::CreatePipe(Process* Owner, int64_t ReadFileDescriptor, int64_t WriteFileDescriptor, INode** ReadNodeOut, INode** WriteNodeOut)
{
	if (Owner == nullptr || ReadNodeOut == nullptr || WriteNodeOut == nullptr || ReadFileDescriptor < 0 || WriteFileDescriptor < 0)
	{
		return LINUX_PIPE_ERR_EINVAL;
	}

	*ReadNodeOut  = nullptr;
	*WriteNodeOut = nullptr;

	if ((PipeEndpointCount + 2) > MAX_TRACKED_PIPES)
	{
		return LINUX_PIPE_ERR_EMFILE;
	}

	PipeKernelObject* Pipe = new PipeKernelObject;
	if (Pipe == nullptr)
	{
		return LINUX_PIPE_ERR_ENOMEM;
	}

	Pipe->ReadOffset         = 0;
	Pipe->WriteOffset        = 0;
	Pipe->BufferedBytes      = 0;
	Pipe->ReaderReferenceCount = 1;
	Pipe->WriterReferenceCount = 1;
	Pipe->ReadEndpoint       = nullptr;
	Pipe->WriteEndpoint      = nullptr;
	Pipe->ReadNode           = nullptr;
	Pipe->WriteNode          = nullptr;

	PipeEndpoint* ReadEndpoint = new PipeEndpoint;
	if (ReadEndpoint == nullptr)
	{
		delete Pipe;
		return LINUX_PIPE_ERR_ENOMEM;
	}

	PipeEndpoint* WriteEndpoint = new PipeEndpoint;
	if (WriteEndpoint == nullptr)
	{
		delete ReadEndpoint;
		delete Pipe;
		return LINUX_PIPE_ERR_ENOMEM;
	}

	INode* ReadNode = new INode;
	if (ReadNode == nullptr)
	{
		delete WriteEndpoint;
		delete ReadEndpoint;
		delete Pipe;
		return LINUX_PIPE_ERR_ENOMEM;
	}

	INode* WriteNode = new INode;
	if (WriteNode == nullptr)
	{
		delete ReadNode;
		delete WriteEndpoint;
		delete ReadEndpoint;
		delete Pipe;
		return LINUX_PIPE_ERR_ENOMEM;
	}

	ReadEndpoint->Pipe      = Pipe;
	ReadEndpoint->IsReadEnd = true;

	WriteEndpoint->Pipe      = Pipe;
	WriteEndpoint->IsReadEnd = false;

	ReadNode->NodeType             = INODE_DEV;
	ReadNode->LinkReferenceCount   = 1;
	ReadNode->UsesVirtualHardLinks = false;
	ReadNode->NodeSize             = 0;
	ReadNode->NodeData             = ReadEndpoint;
	ReadNode->INodeOps             = nullptr;
	ReadNode->FileOps              = &PipeFileOperations;

	WriteNode->NodeType             = INODE_DEV;
	WriteNode->LinkReferenceCount   = 1;
	WriteNode->UsesVirtualHardLinks = false;
	WriteNode->NodeSize             = 0;
	WriteNode->NodeData             = WriteEndpoint;
	WriteNode->INodeOps             = nullptr;
	WriteNode->FileOps              = &PipeFileOperations;

	Pipe->ReadEndpoint = ReadEndpoint;
	Pipe->WriteEndpoint = WriteEndpoint;
	Pipe->ReadNode = ReadNode;
	Pipe->WriteNode = WriteNode;

	PipeEndpointList[PipeEndpointCount].Owner          = Owner;
	PipeEndpointList[PipeEndpointCount].FileDescriptor = ReadFileDescriptor;
	PipeEndpointList[PipeEndpointCount].Node           = ReadNode;
	PipeEndpointList[PipeEndpointCount].Endpoint       = ReadEndpoint;
	++PipeEndpointCount;

	PipeEndpointList[PipeEndpointCount].Owner          = Owner;
	PipeEndpointList[PipeEndpointCount].FileDescriptor = WriteFileDescriptor;
	PipeEndpointList[PipeEndpointCount].Node           = WriteNode;
	PipeEndpointList[PipeEndpointCount].Endpoint       = WriteEndpoint;
	++PipeEndpointCount;

	*ReadNodeOut  = ReadNode;
	*WriteNodeOut = WriteNode;
	return 0;
}

int64_t InterProcessComunicationManager::DuplicatePipeDescriptor(Process* Owner, int64_t FileDescriptor, const File* SourceFile)
{
	if (Owner == nullptr || SourceFile == nullptr || SourceFile->Node == nullptr || FileDescriptor < 0)
	{
		return LINUX_PIPE_ERR_EINVAL;
	}

	if (SourceFile->Node->FileOps != &PipeFileOperations)
	{
		return 0;
	}

	if (SourceFile->Node->NodeData == nullptr)
	{
		return LINUX_PIPE_ERR_EBADF;
	}

	PipeEndpoint* Endpoint = nullptr;
	for (uint64_t Index = 0; Index < PipeEndpointCount; ++Index)
	{
		if (PipeEndpointList[Index].Node != SourceFile->Node)
		{
			continue;
		}

		if (PipeEndpointList[Index].Endpoint != SourceFile->Node->NodeData)
		{
			continue;
		}

		Endpoint = reinterpret_cast<PipeEndpoint*>(SourceFile->Node->NodeData);
		break;
	}

	if (Endpoint == nullptr)
	{
		return 0;
	}

	if (PipeEndpointCount >= MAX_TRACKED_PIPES)
	{
		return LINUX_PIPE_ERR_EMFILE;
	}

	if (Endpoint->Pipe == nullptr)
	{
		return LINUX_PIPE_ERR_EBADF;
	}

	if (Endpoint->IsReadEnd)
	{
		++Endpoint->Pipe->ReaderReferenceCount;
	}
	else
	{
		++Endpoint->Pipe->WriterReferenceCount;
	}

	PipeEndpointList[PipeEndpointCount].Owner          = Owner;
	PipeEndpointList[PipeEndpointCount].FileDescriptor = FileDescriptor;
	PipeEndpointList[PipeEndpointCount].Node           = SourceFile->Node;
	PipeEndpointList[PipeEndpointCount].Endpoint       = Endpoint;
	++PipeEndpointCount;

	return 0;
}

bool InterProcessComunicationManager::ClosePipe(Process* Owner, int64_t FileDescriptor, const INode* Node)
{
	if (Owner == nullptr || FileDescriptor < 0 || Node == nullptr)
	{
		return false;
	}

	for (uint64_t Index = 0; Index < PipeEndpointCount; ++Index)
	{
		if (PipeEndpointList[Index].Owner != Owner || PipeEndpointList[Index].FileDescriptor != FileDescriptor || PipeEndpointList[Index].Node != Node)
		{
			continue;
		}

		PipeEndpoint*   Endpoint = reinterpret_cast<PipeEndpoint*>(PipeEndpointList[Index].Endpoint);
		PipeKernelObject* Pipe   = (Endpoint != nullptr) ? Endpoint->Pipe : nullptr;

		if (Pipe != nullptr)
		{
			if (Endpoint->IsReadEnd)
			{
				if (Pipe->ReaderReferenceCount > 0)
				{
					--Pipe->ReaderReferenceCount;
				}
			}
			else
			{
				if (Pipe->WriterReferenceCount > 0)
				{
					--Pipe->WriterReferenceCount;
				}
			}
		}

		for (uint64_t ShiftIndex = Index + 1; ShiftIndex < PipeEndpointCount; ++ShiftIndex)
		{
			PipeEndpointList[ShiftIndex - 1] = PipeEndpointList[ShiftIndex];
		}

		--PipeEndpointCount;
		PipeEndpointList[PipeEndpointCount].Owner          = nullptr;
		PipeEndpointList[PipeEndpointCount].FileDescriptor = -1;
		PipeEndpointList[PipeEndpointCount].Node           = nullptr;
		PipeEndpointList[PipeEndpointCount].Endpoint       = nullptr;

		if (Pipe != nullptr && Pipe->ReaderReferenceCount == 0 && Pipe->WriterReferenceCount == 0)
		{
			if (Pipe->ReadNode != nullptr)
			{
				Pipe->ReadNode->NodeData = nullptr;
				delete Pipe->ReadNode;
				Pipe->ReadNode = nullptr;
			}

			if (Pipe->WriteNode != nullptr)
			{
				Pipe->WriteNode->NodeData = nullptr;
				delete Pipe->WriteNode;
				Pipe->WriteNode = nullptr;
			}

			delete Pipe->ReadEndpoint;
			delete Pipe->WriteEndpoint;
			delete Pipe;
		}

		return true;
	}

	return false;
}

uint64_t InterProcessComunicationManager::GetSocketCount() const
{
	return SocketCount;
}
