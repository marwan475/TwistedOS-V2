/**
 * File: InterProcessComunicationManager.cpp
 * Author: Marwan Mostafa
 * Description: Inter-process communication manager implementation.
 */

#include "InterProcessComunicationManager.hpp"

namespace
{
constexpr uint64_t LINUX_SOCKADDR_FAMILY_SIZE = sizeof(uint16_t);

uint16_t ReadLinuxAddressFamily(const void* SocketAddress)
{
	if (SocketAddress == nullptr)
	{
		return 0;
	}

	const uint8_t* AddressBytes = reinterpret_cast<const uint8_t*>(SocketAddress);
	return static_cast<uint16_t>(static_cast<uint16_t>(AddressBytes[0]) | (static_cast<uint16_t>(AddressBytes[1]) << 8));
}

uint64_t ComputeBoundUnixPathLength(const char* Path, uint64_t MaxLength)
{
	if (Path == nullptr)
	{
		return 0;
	}

	uint64_t Length = 0;
	while (Length < MaxLength && Path[Length] != '\0')
	{
		++Length;
	}

	return Length;
}

bool AreUnixPathsEqual(const char* LeftPath, uint64_t LeftPathLength, const char* RightPath, uint64_t RightPathLength)
{
	if (LeftPath == nullptr || RightPath == nullptr)
	{
		return false;
	}

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

int64_t SocketRead(File* OpenFile, void* Buffer, uint64_t Count)
{
	(void) OpenFile;
	(void) Buffer;
	(void) Count;
	return LINUX_SOCKET_ERR_EBADF;
}

int64_t SocketWrite(File* OpenFile, const void* Buffer, uint64_t Count)
{
	(void) OpenFile;
	(void) Buffer;
	(void) Count;
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
	: SocketCount(0)
{
	for (uint64_t Index = 0; Index < MAX_TRACKED_SOCKETS; ++Index)
	{
		SocketList[Index] = nullptr;
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

		delete SocketImplementation;
	}
	else if (SocketEntry->Domain == LINUX_AF_INET)
	{
		delete reinterpret_cast<NetworkSocket*>(SocketEntry->Implementation);
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

	if (Domain != LINUX_AF_UNIX && Domain != LINUX_AF_INET)
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
		SocketImplementation->IsBound = false;
		NewSocket->Implementation  = SocketImplementation;
	}
	else
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
		NewSocket->Implementation        = SocketImplementation;
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
		uint64_t               PathLength       = ComputeBoundUnixPathLength(UnixAddress->Path, AvailablePathLen);

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

			if (AreUnixPathsEqual(CandidateUnix->Path, CandidateUnix->PathLength, UnixAddress->Path, PathLength))
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
		SocketImplementation->IsBound    = true;
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

uint64_t InterProcessComunicationManager::GetSocketCount() const
{
	return SocketCount;
}
