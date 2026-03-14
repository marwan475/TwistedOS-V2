#pragma once

#include <stdint.h>

struct SleepTag{
    uint8_t Id;
    uint64_t WaitTicksRemaining;
    SleepTag* Next;
};

class SynchronizationManager
{

private:
    SleepTag* SleepQueue;

public:
    SynchronizationManager();
    ~SynchronizationManager();

    void AddToSleepQueue(uint8_t Id, uint64_t WaitTicks);
    void RemoveFromSleepQueue(uint8_t Id);
    void Tick();
    uint8_t GetNextProcessToWake();
};