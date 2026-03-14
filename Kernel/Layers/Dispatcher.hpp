#pragma once

#include <Logging/FrameBufferConsole.hpp>
#include <stdint.h>

class PhysicalMemoryManager;
class VirtualMemoryManager;

#include "Logic/LogicLayer.hpp"
#include "Resource/ResourceLayer.hpp"
#include "Translation/TranslationLayer.hpp"

struct DispatcherParameters
{
    PhysicalMemoryManager* PMM;
    VirtualMemoryManager*  VMM;
    FrameBufferConsole*    Console;
    uint64_t               KernelHeapVirtualAddrStart;
    uint64_t               KernelHeapVirtualAddrEnd;
};

class Dispatcher
{
private:
    ResourceLayer      Resource;
    LogicLayer         Logic;
    TranslationLayer   Translation;
    static Dispatcher* ActiveDispatcher;
    uint64_t Ticks = 0;

public:
    Dispatcher();
    static void        SetActive(Dispatcher* dispatcher);
    static Dispatcher* GetActive();
    void               InitResourceLayer(const DispatcherParameters& Params);
    void               InitLogicLayer();
    void               InitTranslationLayer();
    void               InitializeLayers(const DispatcherParameters& Params);
    void               InterruptHandler(uint64_t InterruptNumber);

    ResourceLayer*    GetResourceLayer();
    LogicLayer*       GetLogicLayer();
    TranslationLayer* GetTranslationLayer();

    const ResourceLayer*    GetResourceLayer() const;
    const LogicLayer*       GetLogicLayer() const;
    const TranslationLayer* GetTranslationLayer() const;
};