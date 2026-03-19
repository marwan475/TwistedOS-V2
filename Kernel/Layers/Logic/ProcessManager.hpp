/**
 * File: ProcessManager.hpp
 * Author: Marwan Mostafa
 * Description: Process creation and lifecycle management declarations.
 */

#pragma once

#include "VirtualFileSystem.hpp"

#include <Arch/x86.hpp>
#include <Layers/Resource/VirtualAddressSpace.hpp>
#include <stddef.h>
#include <stdint.h>

static constexpr size_t   MAX_PROCESSES                  = 32;
static constexpr uint8_t  PROCESS_ID_INVALID             = 0xFF;
static constexpr size_t   KERNEL_PROCESS_STACK_SIZE      = 4096;
static constexpr size_t   USER_PROCESS_STACK_SIZE        = 8192;
static constexpr size_t   USER_PROCESS_HEAP_SIZE         = 8192;
static constexpr uint64_t USER_PROCESS_VIRTUAL_BASE      = 0x400000;
static constexpr uint64_t USER_PROCESS_VIRTUAL_STACK_TOP = 0x00007FFFFFFFFFFF;
static constexpr size_t   MAX_OPEN_FILES_PER_PROCESS     = 16;

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

struct Process
{
    uint8_t              Id;
    uint8_t              ParrentId;
    bool                 WaitingForChild = false;
    CpuState             State;
    ProcessState         Status;
    ProcessLevel         Level;
    FILE_TYPE            FileType;
    void*                StackPointer;
    VirtualAddressSpace* AddressSpace              = nullptr;
    Dentry*              CurrentFileSystemLocation = nullptr;
    File*                FileTable[MAX_OPEN_FILES_PER_PROCESS];
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
    Process* GetRunningProcess();
    void     UpdateCurrentProcessId(uint8_t Id);
    uint8_t  CreateKernelProcess(void* StackPointer, CpuState InitialState);
    uint8_t  CreateUserProcess(void* StackPointer, CpuState InitialState, VirtualAddressSpace* AddressSpace, FILE_TYPE FileType);
    void*    KillProcess(uint8_t Id);
};
