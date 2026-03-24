/**
 * File: TranslationLayer.hpp
 * Author: Marwan Mostafa
 * Description: Translation layer interface declarations between system layers.
 */

#pragma once

#include <stdint.h>

class LogicLayer;

class TranslationLayer
{
private:
    LogicLayer* Logic;

public:
    TranslationLayer();
    void    Initialize(LogicLayer* Logic);
    int64_t HandlePosixSystemCallNumber(uint64_t SystemCallNumber, uint64_t Arg1, uint64_t Arg2, uint64_t Arg3, uint64_t Arg4, uint64_t Arg5, uint64_t Arg6);

    // Posix system call handlers
    int64_t HandleReadSystemCall(uint64_t FileDescriptor, void* Buffer, uint64_t Count);
    int64_t HandleWriteSystemCall(uint64_t FileDescriptor, const void* Buffer, uint64_t Count);
    int64_t HandleWritevSystemCall(uint64_t FileDescriptor, const void* Iov, uint64_t IovCount);
    int64_t HandlePollSystemCall(void* PollFdArray, uint64_t PollFdCount, int64_t TimeoutMilliseconds);
    int64_t HandleEpollCreate1SystemCall(int64_t Flags);
    int64_t HandleEpollCtlSystemCall(uint64_t EpollFileDescriptor, int64_t Operation, uint64_t TargetFileDescriptor, const void* Event);
    int64_t HandleLseekSystemCall(uint64_t FileDescriptor, int64_t Offset, int64_t Whence);
    int64_t HandleIoctlSystemCall(uint64_t FileDescriptor, uint64_t Request, uint64_t Argument);
    int64_t HandleSocketSystemCall(int64_t Domain, int64_t Type, int64_t Protocol);
    int64_t HandleConnectSystemCall(uint64_t FileDescriptor, const void* SocketAddress, uint64_t SocketAddressLength);
    int64_t HandleAcceptSystemCall(uint64_t FileDescriptor, void* SocketAddress, void* SocketAddressLength);
    int64_t HandleShutdownSystemCall(uint64_t FileDescriptor, int64_t How);
    int64_t HandleBindSystemCall(uint64_t FileDescriptor, const void* SocketAddress, uint64_t SocketAddressLength);
    int64_t HandleListenSystemCall(uint64_t FileDescriptor, int64_t Backlog);
    int64_t HandleGetsocknameSystemCall(uint64_t FileDescriptor, void* SocketAddress, void* SocketAddressLength);
    int64_t HandleSetsockoptSystemCall(uint64_t FileDescriptor, int64_t Level, int64_t OptionName, const void* OptionValue, uint64_t OptionLength);
    int64_t HandleGetsockoptSystemCall(uint64_t FileDescriptor, int64_t Level, int64_t OptionName, void* OptionValue, void* OptionLength);
    int64_t HandleOpenSystemCall(const char* Path, uint64_t Flags);
    int64_t HandleOpenAtSystemCall(int64_t DirectoryFileDescriptor, const char* Path, uint64_t Flags, uint64_t Mode);
    int64_t HandleStatSystemCall(const char* Path, void* Buffer);
    int64_t HandleFstatSystemCall(uint64_t FileDescriptor, void* Buffer);
    int64_t HandleLstatSystemCall(const char* Path, void* Buffer);
    int64_t HandleNewFstatatSystemCall(int64_t DirectoryFileDescriptor, const char* Path, void* Buffer, int64_t Flags);
    int64_t HandleGetdents64SystemCall(uint64_t FileDescriptor, void* Buffer, uint64_t BufferSize);
    int64_t HandleFcntlSystemCall(uint64_t FileDescriptor, uint64_t Command, uint64_t Argument);
    int64_t HandleCloseSystemCall(uint64_t FileDescriptor);
    int64_t HandleGetcwdSystemCall(char* Buffer, uint64_t Size);
    int64_t HandleChdirSystemCall(const char* Path);
    int64_t HandleAccessSystemCall(const char* Path, int64_t Mode);
    int64_t HandleMkdirSystemCall(const char* Path, uint64_t Mode);
    int64_t HandleRmdirSystemCall(const char* Path);
    int64_t HandleLinkSystemCall(const char* OldPath, const char* NewPath);
    int64_t HandleReadlinkSystemCall(const char* Path, char* Buffer, uint64_t BufferSize);
    int64_t HandleUnlinkSystemCall(const char* Path);
    int64_t HandleChmodSystemCall(const char* Path, uint64_t Mode);
    int64_t HandleFchmodSystemCall(uint64_t FileDescriptor, uint64_t Mode);
    int64_t HandleRenameSystemCall(const char* OldPath, const char* NewPath);
    int64_t HandleUtimeSystemCall(const char* Path, const void* Times);
    int64_t HandleUtimesSystemCall(const char* Path, const void* Times);
    int64_t HandleFutimesatSystemCall(int64_t DirectoryFileDescriptor, const char* Path, const void* Times);
    int64_t HandleUtimensatSystemCall(int64_t DirectoryFileDescriptor, const char* Path, const void* Times, int64_t Flags);
    int64_t HandleChrootSystemCall(const char* Path);
    int64_t HandleMountSystemCall(const char* Source, const char* Target, const char* FileSystemType, uint64_t MountFlags, const void* Data);
    int64_t HandleMprotectSystemCall(void* Address, uint64_t Length, int64_t Protection);
    int64_t HandleBrkSystemCall(uint64_t Address);
    int64_t HandleGetuidSystemCall();
    int64_t HandleGetgidSystemCall();
    int64_t HandleUnameSystemCall(void* Buffer);
    int64_t HandleGetpidSystemCall();
    int64_t HandleGetppidSystemCall();
    int64_t HandleKillSystemCall(int64_t Pid, int64_t Signal);
    int64_t HandleSetpgidSystemCall(int64_t Pid, int64_t ProcessGroupId);
    int64_t HandleGetprioritySystemCall(int64_t Which, int64_t Who);
    int64_t HandleSetprioritySystemCall(int64_t Which, int64_t Who, int64_t NiceValue);
    int64_t HandleUmaskSystemCall(uint64_t Mask);
    int64_t HandleGetrlimitSystemCall(int64_t Resource, void* Limit);
    int64_t HandlePrlimit64SystemCall(int64_t Pid, int64_t Resource, const void* NewLimit, void* OldLimit);
    int64_t HandleGetpgrpSystemCall();
    int64_t HandleSetsidSystemCall();
    int64_t HandleGetpgidSystemCall(int64_t Pid);
    int64_t HandleGetsidSystemCall(int64_t Pid);
    int64_t HandleGeteuidSystemCall();
    int64_t HandleGetegidSystemCall();
    int64_t HandleDup2SystemCall(uint64_t OldFileDescriptor, uint64_t NewFileDescriptor);
    int64_t HandlePauseSystemCall();
    int64_t HandleNanosleepSystemCall(const void* RequestedTime, void* RemainingTime);
    int64_t HandleSetitimerSystemCall(int64_t Which, const void* NewValue, void* OldValue);
    int64_t HandleGettimeofdaySystemCall(void* TimeValue, void* TimeZone);
    int64_t HandleClockGettimeSystemCall(int64_t ClockId, void* TimeSpec);
    int64_t HandleClockGetresSystemCall(int64_t ClockId, void* TimeSpec);
    int64_t HandleForkSystemCall();
    int64_t HandleVforkSystemCall();
    int64_t HandleExitGroupSystemCall(int64_t Status);
    int64_t HandleExecveSystemCall(const char* Path, const char* const* Argv, const char* const* Envp);
    int64_t HandleWaitSystemCall(int64_t Pid, int* Status, int64_t Options, void* Rusage);
    int64_t HandleMmapSystemCall(void* Address, uint64_t Length, int64_t Protection, int64_t Flags, int64_t FileDescriptor, int64_t Offset);
    int64_t HandleMunmapSystemCall(void* Address, uint64_t Length);
    int64_t HandleRtSigactionSystemCall(int64_t Signal, const void* Action, void* OldAction, uint64_t SigsetSize);
    int64_t HandleRtSigprocmaskSystemCall(int64_t How, const void* Set, void* OldSet, uint64_t SigsetSize);
    int64_t HandleRtSigsuspendSystemCall(const void* Set, uint64_t SigsetSize);
    int64_t HandleArchPrctlSystemCall(uint64_t Code, uint64_t Address);
    int64_t HandleSetTidAddressSystemCall(int* TidPointer);

    LogicLayer* GetLogicLayer() const;
};