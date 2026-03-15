#pragma once

#include <Arch/x86.hpp>
#include <stddef.h>
#include <stdint.h>

#define MAX_PROCESSES 32
#define PROCESS_STACK_SIZE 4096

enum ProcessState
{
    PROCESS_RUNNING,
    PROCESS_READY,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
};

struct Process
{
    uint8_t      Id;
    CpuState     State;
    ProcessState Status;
    void*        StackPointer;
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
    void UpdateCurrentProcessId(uint8_t Id);
    uint8_t  CreateProcess(void* StackPointer, CpuState InitialState);
    void*    KillProcess(uint8_t Id);
};
