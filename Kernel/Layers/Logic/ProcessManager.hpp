#pragma once

#include <Arch/x86.hpp>
#include <Layers/Resource/VirtualAddressSpace.hpp>
#include <stddef.h>
#include <stdint.h>

#define MAX_PROCESSES 32
#define KERNEL_PROCESS_STACK_SIZE 4096
#define USER_PROCESS_STACK_SIZE 8192
#define USER_PROCESS_HEAP_SIZE 8192
#define USER_PROCESS_VIRTUAL_BASE 0x400000

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

struct Process
{
    uint8_t              Id;
    CpuState             State;
    ProcessState         Status;
    ProcessLevel         Level;
    void*                StackPointer;
    VirtualAddressSpace* AddressSpace = nullptr;
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
    uint8_t  CreateUserProcess(void* StackPointer, CpuState InitialState, VirtualAddressSpace* AddressSpace);
    void*    KillProcess(uint8_t Id);
};
