#pragma once

#include "ProcessManager.hpp"
#include "Scheduler.hpp"

#include <Arch/x86.hpp>
#include <stdint.h>

class ResourceLayer;

class LogicLayer
{
private:
    ResourceLayer*  Resource;
    ProcessManager* PM;
    Scheduler*      Sched;
    bool            IsScheduling = false;

public:
    LogicLayer();
    ~LogicLayer();
    void Initialize(ResourceLayer* Resource);

    ResourceLayer* GetResourceLayer() const;
    void           InitializeProcessManager();
    void           InitializeScheduler();
    uint8_t        CreateProcess(void (*EntryPoint)(), bool IsUserProcess = false);
    bool           RunProcess(uint8_t Id);
    void           KillProcess(uint8_t Id);
    void           Schedule();
    bool           isScheduling();
    void           EnableScheduling();
    void           DisableScheduling();
};