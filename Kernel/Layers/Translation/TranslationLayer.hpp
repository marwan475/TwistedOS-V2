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
    int64_t HandleOpenSystemCall(const char* Path, uint64_t Flags);
    int64_t HandleCloseSystemCall(uint64_t FileDescriptor);
    int64_t HandleMprotectSystemCall(void* Address, uint64_t Length, int64_t Protection);
    int64_t HandleBrkSystemCall(uint64_t Address);
    int64_t HandleGetuidSystemCall();
    int64_t HandleGetgidSystemCall();
    int64_t HandleGetpidSystemCall();
    int64_t HandleGeteuidSystemCall();
    int64_t HandleGetegidSystemCall();
    int64_t HandleDup2SystemCall(uint64_t OldFileDescriptor, uint64_t NewFileDescriptor);
    int64_t HandleForkSystemCall();
    int64_t HandleExecveSystemCall(const char* Path, const char* const* Argv, const char* const* Envp);
    int64_t HandleWaitSystemCall(int* Status);
    int64_t HandleMmapSystemCall(void* Address, uint64_t Length, int64_t Protection, int64_t Flags, int64_t FileDescriptor, int64_t Offset);
    int64_t HandleRtSigactionSystemCall(int64_t Signal, const void* Action, void* OldAction, uint64_t SigsetSize);
    int64_t HandleRtSigprocmaskSystemCall(int64_t How, const void* Set, void* OldSet, uint64_t SigsetSize);
    int64_t HandleArchPrctlSystemCall(uint64_t Code, uint64_t Address);
    int64_t HandleSetTidAddressSystemCall(int* TidPointer);

    LogicLayer* GetLogicLayer() const;
};