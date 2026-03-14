#pragma once

#include "ProcessManager.hpp"
#include "Scheduler.hpp"
#include "SynchronizationManager.hpp"

#include <Arch/x86.hpp>
#include <stdint.h>

class ResourceLayer;

class LogicLayer
{
private:
    ResourceLayer*          Resource;
    ProcessManager*         PM;
    Scheduler*              Sched;
    SynchronizationManager* Sync;
    bool                    IsScheduling = false;

public:
    LogicLayer();
    ~LogicLayer();
    void Initialize(ResourceLayer* Resource);

    ResourceLayer* GetResourceLayer() const;
    void           InitializeProcessManager();
    void           InitializeScheduler();
    void           InitializeSynchronizationManager();
    uint8_t        CreateProcess(void (*EntryPoint)());
    bool           RunProcess(uint8_t Id);
    void           KillProcess(uint8_t Id);
    void           SleepProcess(uint8_t Id, uint64_t WaitTicks);
    void           WakeProcess(uint8_t Id);
    void           Tick();
    void           Schedule();
    bool           isScheduling();
    void           EnableScheduling();
    void           DisableScheduling();
};