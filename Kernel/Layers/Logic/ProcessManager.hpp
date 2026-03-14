#pragma once

#include <Arch/x86.hpp>

#include <stddef.h>
#include <stdint.h>

enum ProcessState
{
    PROCESS_RUNNING,
    PROCESS_READY,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
};

struct Process
{
    uint8_t     Id;
    CpuState     State;
    ProcessState Status;
    void*        StackPointer;
};

class ProcessManager
{
private:
    static constexpr size_t MaxProcesses = 256;
    Process                 Processes[MaxProcesses];

public:
    ProcessManager();

    size_t   GetMaxProcesses() const;
    Process* GetProcessById(uint8_t Id);
    uint8_t  CreateProcess(void* StackPointer , CpuState InitialState);
    void*  KillProcess(uint8_t Id);
};
