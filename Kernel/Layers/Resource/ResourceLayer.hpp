/**
 * File: ResourceLayer.hpp
 * Author: Marwan Mostafa
 * Description: Resource layer coordination interface declarations.
 */

#pragma once

#include "DeviceManager.hpp"
#include "ExtendedFileSystemManager.hpp"
#include "FrameBuffer.hpp"
#include "KernelHeapManager.hpp"
#include "Keyboard.hpp"
#include "PartitionManager.hpp"
#include "RamFileSystemManager.hpp"
#include "TTY.hpp"

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
    PhysicalMemoryManager*     PMM;
    VirtualMemoryManager*      VMM;
    FrameBufferConsole*        Console;
    uint64_t                   KernelHeapVirtualAddrStart;
    uint64_t                   KernelHeapVirtualAddrEnd;
    uint64_t                   KernelPageMapL4TableAddr;
    uint64_t                   InitramfsAddress;
    uint64_t                   InitramfsSize;
    FrameBuffer                FB;
    KernelHeapManager          KHM;
    RamFileSystemManager       RFS;
    ExtendedFileSystemManager* EFSManager;
    TTY*                       Terminal;
    Keyboard*                  InputKeyboard;
    DeviceManager*             DevManager;
    PartitionManager           PartManager;

public:
    ResourceLayer();
    void Initialize(PhysicalMemoryManager* PMM, VirtualMemoryManager* VMM, FrameBufferConsole* Console, uint64_t KernelHeapVirtualAddrStart, uint64_t KernelHeapVirtualAddrEnd,
                    uint64_t InitramfsAddress, uint64_t InitramfsSize);
    void InitializeFrameBuffer(const EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE& GopMode);

    PhysicalMemoryManager*     GetPMM() const;
    VirtualMemoryManager*      GetVMM() const;
    FrameBufferConsole*        GetConsole() const;
    FrameBuffer*               GetFrameBuffer();
    uint64_t                   GetKernelHeapVirtualAddrStart() const;
    uint64_t                   GetKernelHeapVirtualAddrEnd() const;
    RamFileSystemManager*      GetRamFileSystemManager();
    TTY*                       GetTTY() const;
    Keyboard*                  GetKeyboard() const;
    void                       InitializeKernelHeapManager();
    void                       InitializeRamFileSystemManager();
    void                       InitializeTTY();
    void                       InitializeKeyboard();
    void                       InitializeDeviceManager();
    void                       InitPartitionManager();
    bool                       InitializeRootFileSystemManager(const RootFileSystemPartitionInfo* PartitionInfo);
    bool                       LocateRootFileSystemPartition(RootFileSystemPartitionInfo* PartitionInfo);
    PartitionManager*          GetPartitionManager();
    DeviceManager*             GetDeviceManager() const;
    ExtendedFileSystemManager* GetExtendedFileSystemManager() const;
    void*                      kmalloc(size_t Size);
    void                       kfree(void* Ptr);
    uint64_t                   ReadCurrentPageTable() const;
    void                       LoadPageTable(uint64_t PageMapL4TableAddr);
    void                       LoadKernelPageTable();
    void*                      LoadFileFromInitramfs(const char* Path, uint64_t* Size);
    void                       TaskSwitchKernel(CpuState* OldState, const CpuState& NewState);
    void                       TaskSwitchKernelCurrentAddressSpace(CpuState* OldState, const CpuState& NewState);
    void                       TaskSwitchUser(CpuState* OldState, const CpuState& NewState, const VirtualAddressSpace* NewAddressSpace);
};