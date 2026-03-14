#pragma once

#include "IntrusiveQueue.hpp"
#include "ProcessManager.hpp"

#include <stdint.h>

struct ProcessTag
{
    uint8_t     Id;
    ProcessTag* NextProcess;
};

class Scheduler
{
private:
    IntrusiveQueue<ProcessTag, &ProcessTag::NextProcess> ReadyQueue;
    ProcessTag*                                          CurrentProcess;

public:
    Scheduler();
    ~Scheduler();

    void    AddToReadyQueue(uint8_t ProcessId);
    bool    RemoveFromReadyQueue(uint8_t ProcessId);
    uint8_t SelectNextProcess();
};
