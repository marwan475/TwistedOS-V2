#pragma once

#include "KernelHeapManager.hpp"
#include "RamFileSystemManager.hpp"

#include <Arch/x86.hpp>
#include <Logging/FrameBufferConsole.hpp>
#include <stdint.h>

class PhysicalMemoryManager;
class VirtualMemoryManager;
class FrameBufferConsole;
class VirtualAddressSpace;

class ResourceLayer
{
private:
    PhysicalMemoryManager* PMM;
    VirtualMemoryManager*  VMM;
    FrameBufferConsole*    Console;
    uint64_t               KernelHeapVirtualAddrStart;
    uint64_t               KernelHeapVirtualAddrEnd;
    uint64_t               InitramfsAddress;
    uint64_t               InitramfsSize;
    KernelHeapManager      KHM;
    RamFileSystemManager   RFS;

public:
    ResourceLayer();
    void Initialize(PhysicalMemoryManager* PMM, VirtualMemoryManager* VMM, FrameBufferConsole* Console, uint64_t KernelHeapVirtualAddrStart, uint64_t KernelHeapVirtualAddrEnd,
                    uint64_t InitramfsAddress, uint64_t InitramfsSize);

    PhysicalMemoryManager* GetPMM() const;
    VirtualMemoryManager*  GetVMM() const;
    FrameBufferConsole*    GetConsole() const;
    uint64_t               GetKernelHeapVirtualAddrStart() const;
    uint64_t               GetKernelHeapVirtualAddrEnd() const;
    void                   InitializeKernelHeapManager();
    void                   InitializeRamFileSystemManager();
    void*                  kmalloc(size_t Size);
    void                   kfree(void* Ptr);
    void*                  LoadFileFromInitramfs(const char* Path, uint64_t* Size);
    void                   TaskSwitchKernel(CpuState* OldState, const CpuState& NewState);
    void                   TaskSwitchUser(CpuState* OldState, const CpuState& NewState, const VirtualAddressSpace* NewAddressSpace);
};