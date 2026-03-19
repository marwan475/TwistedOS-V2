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

struct WaitForTTYInputTag
{
    uint8_t             Id;
    WaitForTTYInputTag* Next;
};

class SynchronizationManager
{
private:
    IntrusiveQueue<SleepTag, &SleepTag::Next> SleepQueue;
    IntrusiveQueue<WaitForTTYInputTag, &WaitForTTYInputTag::Next> TTYInputWaitQueue;

public:
    SynchronizationManager();
    ~SynchronizationManager();

    void    AddToSleepQueue(uint8_t Id, uint64_t WaitTicks);
    void    RemoveFromSleepQueue(uint8_t Id);
    void    AddToTTYInputWaitQueue(uint8_t Id);
    void    RemoveFromTTYInputWaitQueue(uint8_t Id);
    void    Tick();
    uint8_t GetNextProcessToWake();
    uint8_t GetNextTTYInputWaiter();
};