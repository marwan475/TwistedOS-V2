/**
 * File: ProcessManager.hpp
 * Author: Marwan Mostafa
 * Description: Process creation and lifecycle management declarations.
 */

#pragma once

#include "VirtualFileSystem.hpp"

#include <Arch/x86.hpp>
#include <KernelParameters.hpp>
#include <Layers/Resource/VirtualAddressSpace.hpp>
#include <stddef.h>
#include <stdint.h>

static constexpr size_t  MAX_PROCESSES                     = 32;
static constexpr uint8_t PROCESS_ID_INVALID                = 0xFF;
static constexpr size_t  MAX_OPEN_FILES_PER_PROCESS        = 16;
static constexpr size_t  MAX_MEMORY_MAPPINGS_PER_PROCESS   = 1024;
static constexpr size_t  MAX_POSIX_SIGNALS_PER_PROCESS     = 64;
static constexpr size_t  PROCESS_KERNEL_SYSCALL_STACK_SIZE = 65536;
static constexpr size_t  MAX_PROCESS_RESOURCE_LIMITS        = 16;

enum ProcessState
{
    PROCESS_RUNNING,
    PROCESS_READY,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
};

enum ProcessLevel
{
    PROCESS_LEVEL_KERNEL,
    PROCESS_LEVEL_USER
};

enum FILE_TYPE
{
    FILE_TYPE_RAW_BINARY,
    FILE_TYPE_ELF
};

struct ProcessSavedSystemCallFrame
{
    uint64_t UserRSP;
    uint64_t UserRIP;
    uint64_t UserRFLAGS;
    uint64_t UserRAX;
    uint64_t UserRDX;
    uint64_t UserRBX;
    uint64_t UserRBP;
    uint64_t UserRSI;
    uint64_t UserRDI;
    uint64_t UserR8;
    uint64_t UserR9;
    uint64_t UserR10;
    uint64_t UserR12;
    uint64_t UserR13;
    uint64_t UserR14;
    uint64_t UserR15;
};

struct ProcessMemoryMapping
{
    bool     InUse                = false;
    uint64_t VirtualAddressStart  = 0;
    uint64_t Length               = 0;
    uint64_t PhysicalAddressStart = 0;
    bool     IsAnonymous          = false;
};

struct ProcessSignalAction
{
    uint64_t Handler  = 0;
    uint64_t Flags    = 0;
    uint64_t Restorer = 0;
    uint64_t Mask     = 0;
};

struct LinuxKernelSigAction
{
    uint64_t Handler;
    uint64_t Flags;
    uint64_t Restorer;
    uint64_t Mask;
};

struct Process
{
    uint8_t                     Id;
    uint8_t                     ParrentId;
    bool                        WaitingForVforkChild       = false;
    uint8_t                     VforkChildId               = PROCESS_ID_INVALID;
    bool                        IsVforkChild               = false;
    uint8_t                     VforkParentId              = PROCESS_ID_INVALID;
    bool                        WaitingForChild            = false;
    bool                        WaitingForSystemCallReturn = false;
    bool                        HasSavedSystemCallFrame    = false;
    bool                        HasPendingChildExit        = false;
    uint8_t                     PendingChildId             = PROCESS_ID_INVALID;
    int32_t                     PendingChildStatus         = 0;
    ProcessSavedSystemCallFrame SavedSystemCallFrame       = {};
    CpuState                    State;
    ProcessState                Status;
    ProcessLevel                Level;
    FILE_TYPE                   FileType;
    void*                       StackPointer;
    void*                       KernelSystemCallStackBase = nullptr;
    uint64_t                    KernelSystemCallStackTop  = 0;
    uint64_t                    UserFSBase                = 0;
    int32_t                     ProcessGroupId            = 0;
    int32_t                     SessionId                 = 0;
    uint64_t                    BlockedSignalMask         = 0;
    uint64_t                    PendingSignalMask         = 0;
    uint8_t                     DebugSyscallTraceRemaining = 0;
    bool                        DebugIsXorgProcess        = false;
    int*                        ClearChildTidAddress      = nullptr;
    uint64_t                    ProgramBreak              = 0;
    uint64_t                    RealIntervalTimerRemainingTicks = 0;
    uint64_t                    RealIntervalTimerIntervalTicks  = 0;
    uint32_t                    FileCreationMask          = 0022;
    bool                        ResourceLimitsInitialized  = false;
    uint64_t                    ResourceLimitCurrent[MAX_PROCESS_RESOURCE_LIMITS] = {};
    uint64_t                    ResourceLimitMaximum[MAX_PROCESS_RESOURCE_LIMITS] = {};
    VirtualAddressSpace*        AddressSpace              = nullptr;
    Dentry*                     CurrentFileSystemLocation = nullptr;
    Dentry*                     RunningExecutableDentry   = nullptr;
    INode*                      RunningExecutableINode    = nullptr;
    File*                       FileTable[MAX_OPEN_FILES_PER_PROCESS];
    ProcessSignalAction*        SignalActions = nullptr;
    ProcessMemoryMapping        MemoryMappings[MAX_MEMORY_MAPPINGS_PER_PROCESS];
};

class ProcessManager
{
private:
    static constexpr size_t MaxProcesses = MAX_PROCESSES;
    Process                 Processes[MaxProcesses];
    uint8_t                 CurrentProcessId;

public:
    ProcessManager();

    size_t   GetMaxProcesses() const;
    Process* GetProcessById(uint8_t Id);
    Process* GetCurrentProcess();
    Process* GetRunningProcess();
    void     UpdateCurrentProcessId(uint8_t Id);
    uint8_t  CreateKernelProcess(void* StackPointer, CpuState InitialState);
    uint8_t  CreateUserProcess(void* StackPointer, CpuState InitialState, VirtualAddressSpace* AddressSpace, FILE_TYPE FileType);
    void*    KillProcess(uint8_t Id);
};
