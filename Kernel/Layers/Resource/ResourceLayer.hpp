#pragma once

#include "KernelHeapManager.hpp"

#include <Arch/x86.hpp>
#include <Logging/FrameBufferConsole.hpp>
#include <stdint.h>

class PhysicalMemoryManager;
class VirtualMemoryManager;
class FrameBufferConsole;

class ResourceLayer
{
private:
    PhysicalMemoryManager* PMM;
    VirtualMemoryManager*  VMM;
    FrameBufferConsole*    Console;
    uint64_t               KernelHeapVirtualAddrStart;
    uint64_t               KernelHeapVirtualAddrEnd;
    KernelHeapManager      KHM;

public:
    ResourceLayer();
    void Initialize(PhysicalMemoryManager* PMM, VirtualMemoryManager* VMM, FrameBufferConsole* Console,
                    uint64_t KernelHeapVirtualAddrStart, uint64_t KernelHeapVirtualAddrEnd);

    PhysicalMemoryManager* GetPMM() const;
    VirtualMemoryManager*  GetVMM() const;
    FrameBufferConsole*    GetConsole() const;
    uint64_t               GetKernelHeapVirtualAddrStart() const;
    uint64_t               GetKernelHeapVirtualAddrEnd() const;
    void                   InitializeKernelHeapManager();
    void*                  kmalloc(size_t Size);
    void                   kfree(void* Ptr);
    void                   TaskSwitch(CpuState* OldState, const CpuState& NewState);
};