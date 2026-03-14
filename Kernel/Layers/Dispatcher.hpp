#pragma once

#include <stdint.h>

class PhysicalMemoryManager;
class VirtualMemoryManager;
class FrameBufferConsole;

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

public:
    Dispatcher();
    static void        SetActive(Dispatcher* dispatcher);
    static Dispatcher* GetActive();
    void               InitResourceLayer(const DispatcherParameters& Params);
    void               InitLogicLayer();
    void               InitTranslationLayer();
    void               InitializeLayers(const DispatcherParameters& Params);

    ResourceLayer*    GetResourceLayer();
    LogicLayer*       GetLogicLayer();
    TranslationLayer* GetTranslationLayer();

    const ResourceLayer*    GetResourceLayer() const;
    const LogicLayer*       GetLogicLayer() const;
    const TranslationLayer* GetTranslationLayer() const;
};