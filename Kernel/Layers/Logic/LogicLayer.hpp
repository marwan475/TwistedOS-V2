#pragma once

#include <Arch/x86.hpp>

#include <stdint.h>

class ResourceLayer;

enum ProcessState
{
    PROCESS_RUNNING,
    PROCESS_READY,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
};

struct Process
{
    uint64_t     Id;
    CpuState     State;
    ProcessState Status;
    void*        StackPointer;
    Process*     Next;
};

class LogicLayer
{
private:
    ResourceLayer* Resource;

public:
    LogicLayer();
    void Initialize(ResourceLayer* Resource);

    ResourceLayer* GetResourceLayer() const;
};