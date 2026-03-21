/**
 * File: Dispatcher.hpp
 * Author: Marwan Mostafa
 * Description: Cross-layer dispatcher interface declarations.
 */

#pragma once

#include <Arch/x86.hpp>
#include <Logging/FrameBufferConsole.hpp>
#include <stdint.h>
#include <uefi.hpp>

class PhysicalMemoryManager;
class VirtualMemoryManager;

#include "Logic/LogicLayer.hpp"
#include "Resource/ResourceLayer.hpp"
#include "Translation/TranslationLayer.hpp"

struct DispatcherParameters
{
    PhysicalMemoryManager*            PMM;
    VirtualMemoryManager*             VMM;
    FrameBufferConsole*               Console;
    uint64_t                          KernelHeapVirtualAddrStart;
    uint64_t                          KernelHeapVirtualAddrEnd;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GopMode;
    uint64_t                          InitramfsAddress;
    uint64_t                          InitramfsSize;
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
    void               InterruptHandler(uint64_t InterruptNumber);
    void               HandleException(const Registers* Regs);
    int64_t            HandleSystemCall(uint64_t SystemCallNumber, uint64_t Arg1, uint64_t Arg2, uint64_t Arg3, uint64_t Arg4, uint64_t Arg5, uint64_t Arg6);

    ResourceLayer*    GetResourceLayer();
    LogicLayer*       GetLogicLayer();
    TranslationLayer* GetTranslationLayer();

    const ResourceLayer*    GetResourceLayer() const;
    const LogicLayer*       GetLogicLayer() const;
    const TranslationLayer* GetTranslationLayer() const;
};