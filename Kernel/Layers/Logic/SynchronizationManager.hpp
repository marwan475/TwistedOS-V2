/**
 * File: SynchronizationManager.hpp
 * Author: Marwan Mostafa
 * Description: Kernel synchronization primitive declarations.
 */

#pragma once

#include "IntrusiveQueue.hpp"

#include <stdint.h>

struct SleepTag
{
    uint8_t   Id;
    uint64_t  WaitTicksRemaining;
    SleepTag* Next;
};

class SynchronizationManager
{
private:
    IntrusiveQueue<SleepTag, &SleepTag::Next> SleepQueue;

public:
    SynchronizationManager();
    ~SynchronizationManager();

    void    AddToSleepQueue(uint8_t Id, uint64_t WaitTicks);
    void    RemoveFromSleepQueue(uint8_t Id);
    void    Tick();
    uint8_t GetNextProcessToWake();
};