/**
 * File: InterProcessComunicationManager.cpp
 * Author: Marwan Mostafa
 * Description: Inter-process communication manager implementation.
 */

#include "InterProcessComunicationManager.hpp"

namespace
{
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
		delete reinterpret_cast<UnixSocket*>(SocketEntry->Implementation);
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
		NewSocket->Implementation        = SocketImplementation;
	}

	SocketList[SocketCount++] = NewSocket;
	return NewSocket;
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
