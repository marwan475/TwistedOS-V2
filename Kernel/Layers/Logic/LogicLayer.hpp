#pragma once

#include "ProcessManager.hpp"

#include <Arch/x86.hpp>
#include <stdint.h>

class ResourceLayer;

class LogicLayer
{
private:
    ResourceLayer* Resource;
    ProcessManager* PM;

public:
    LogicLayer();
    ~LogicLayer();
    void Initialize(ResourceLayer* Resource);

    ResourceLayer* GetResourceLayer() const;
    void InitializeProcessManager();
    ProcessManager* GetProcessManager() const;
};