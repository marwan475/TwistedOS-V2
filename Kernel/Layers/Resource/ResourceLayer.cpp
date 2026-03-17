/**
 * File: ResourceLayer.cpp
 * Author: Marwan Mostafa
 * Description: Resource layer coordination implementation.
 */

#include "ResourceLayer.hpp"

#include "VirtualAddressSpace.hpp"

#include <CommonUtils.hpp>
#include <Memory/PhysicalMemoryManager.hpp>

extern "C" void ResourceLayerTaskSwitchKernelAsm(CpuState* OldState, const CpuState* NewState);
extern "C" void ResourceLayerTaskSwitchUserAsm(CpuState* OldState, const CpuState* NewState, uint64_t PageMapL4TableAddr);

/**
 * Function: ResourceLayer::ResourceLayer
 * Description: Constructs a resource layer with default uninitialized subsystem pointers and ranges.
 * Parameters:
 *   None
 * Returns:
 *   ResourceLayer - Constructed resource layer instance.
 */
ResourceLayer::ResourceLayer() : PMM(nullptr), VMM(nullptr), Console(nullptr), KernelHeapVirtualAddrStart(0), KernelHeapVirtualAddrEnd(0), KHM(0, 0), RFS(0, 0)
{
}

/**
 * Function: ResourceLayer::Initialize
 * Description: Initializes core resource layer dependencies and memory region metadata.
 * Parameters:
 *   PhysicalMemoryManager* PMM - Physical memory manager instance.
 *   VirtualMemoryManager* VMM - Virtual memory manager instance.
 *   FrameBufferConsole* Console - Console used for output.
 *   uint64_t KernelHeapVirtualAddrStart - Kernel heap virtual range start address.
 *   uint64_t KernelHeapVirtualAddrEnd - Kernel heap virtual range end address.
 *   uint64_t InitramfsAddress - Physical address of initramfs archive.
 *   uint64_t InitramfsSize - Size of initramfs archive in bytes.
 * Returns:
 *   void - No return value.
 */
void ResourceLayer::Initialize(PhysicalMemoryManager* PMM, VirtualMemoryManager* VMM, FrameBufferConsole* Console, uint64_t KernelHeapVirtualAddrStart, uint64_t KernelHeapVirtualAddrEnd,
                               uint64_t InitramfsAddress, uint64_t InitramfsSize)
{
    this->PMM                        = PMM;
    this->VMM                        = VMM;
    this->Console                    = Console;
    this->KernelHeapVirtualAddrStart = KernelHeapVirtualAddrStart;
    this->KernelHeapVirtualAddrEnd   = KernelHeapVirtualAddrEnd;
    this->InitramfsAddress           = InitramfsAddress;
    this->InitramfsSize              = InitramfsSize;
}

/**
 * Function: ResourceLayer::GetPMM
 * Description: Returns the physical memory manager.
 * Parameters:
 *   None
 * Returns:
 *   PhysicalMemoryManager* - Pointer to the physical memory manager.
 */
PhysicalMemoryManager* ResourceLayer::GetPMM() const
{
    return PMM;
}

/**
 * Function: ResourceLayer::GetVMM
 * Description: Returns the virtual memory manager.
 * Parameters:
 *   None
 * Returns:
 *   VirtualMemoryManager* - Pointer to the virtual memory manager.
 */
VirtualMemoryManager* ResourceLayer::GetVMM() const
{
    return VMM;
}

/**
 * Function: ResourceLayer::GetConsole
 * Description: Returns the framebuffer console used by the resource layer.
 * Parameters:
 *   None
 * Returns:
 *   FrameBufferConsole* - Pointer to the active console.
 */
FrameBufferConsole* ResourceLayer::GetConsole() const
{
    return Console;
}

/**
 * Function: ResourceLayer::GetKernelHeapVirtualAddrStart
 * Description: Returns the kernel heap virtual address range start.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - Start address of the kernel heap virtual range.
 */
uint64_t ResourceLayer::GetKernelHeapVirtualAddrStart() const
{
    return KernelHeapVirtualAddrStart;
}

/**
 * Function: ResourceLayer::GetKernelHeapVirtualAddrEnd
 * Description: Returns the kernel heap virtual address range end.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - End address of the kernel heap virtual range.
 */
uint64_t ResourceLayer::GetKernelHeapVirtualAddrEnd() const
{
    return KernelHeapVirtualAddrEnd;
}

/**
 * Function: ResourceLayer::GetRamFileSystemManager
 * Description: Returns the RAM file system manager.
 * Parameters:
 *   None
 * Returns:
 *   RamFileSystemManager* - Pointer to the RAM file system manager.
 */
RamFileSystemManager* ResourceLayer::GetRamFileSystemManager()
{
    return &RFS;
}

/**
 * Function: ResourceLayer::InitializeKernelHeapManager
 * Description: Initializes the kernel heap manager with the configured virtual heap range.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void ResourceLayer::InitializeKernelHeapManager()
{
    KHM = KernelHeapManager(KernelHeapVirtualAddrStart, KernelHeapVirtualAddrEnd);
    Console->printf_("Kernel Heap Manager Initialized\n");
}

/**
 * Function: ResourceLayer::InitializeRamFileSystemManager
 * Description: Initializes the ram file system manager and prints parsed initramfs entries.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void ResourceLayer::InitializeRamFileSystemManager()
{
    RFS = RamFileSystemManager(InitramfsAddress, InitramfsSize);
    Console->printf_("RAM File System Manager Initialized\n");
    RFS.ParseAndPrintInitramfs(Console);
}

/**
 * Function: ResourceLayer::kmalloc
 * Description: Allocates memory from the kernel heap manager.
 * Parameters:
 *   size_t Size - Number of bytes to allocate.
 * Returns:
 *   void* - Pointer to allocated memory or nullptr on failure.
 */
void* ResourceLayer::kmalloc(size_t Size)
{
    return KHM.kmalloc(Size);
}

/**
 * Function: ResourceLayer::kfree
 * Description: Frees memory previously allocated by the kernel heap manager.
 * Parameters:
 *   void* Ptr - Pointer to allocation to free.
 * Returns:
 *   void - No return value.
 */
void ResourceLayer::kfree(void* Ptr)
{
    KHM.kfree(Ptr);
}

// Loads file from initramfs into PMM-allocated physical pages
/**
 * Function: ResourceLayer::LoadFileFromInitramfs
 * Description: Finds an initramfs file, allocates physical pages, and copies file data into them.
 * Parameters:
 *   const char* Path - Path of file inside initramfs.
 *   uint64_t* Size - Output pointer that receives file size in bytes.
 * Returns:
 *   void* - Pointer to PMM-allocated copy of file data, or nullptr on failure.
 */
void* ResourceLayer::LoadFileFromInitramfs(const char* Path, uint64_t* Size)
{
    if (Path == nullptr || Size == nullptr)
    {
        return nullptr;
    }

    const void* Data = nullptr;

    if (!RFS.FindFile(Path, &Data, Size, Console) || Data == nullptr || *Size == 0)
    {
        return nullptr;
    }

    UINTN Pages = (*Size + PAGE_SIZE - 1) / PAGE_SIZE;

    void* AllocatedData = PMM->AllocatePagesFromDescriptor(Pages);
    if (AllocatedData == nullptr)
    {
        return nullptr;
    }

    kmemset(AllocatedData, 0, Pages * PAGE_SIZE);
    memcpy(AllocatedData, Data, *Size);

    return AllocatedData;
}

/**
 * Function: ResourceLayer::TaskSwitchKernel
 * Description: Performs a kernel-task context switch through assembly helper.
 * Parameters:
 *   CpuState* OldState - Storage for outgoing task state.
 *   const CpuState& NewState - Incoming task state to restore.
 * Returns:
 *   void - No return value.
 */
void ResourceLayer::TaskSwitchKernel(CpuState* OldState, const CpuState& NewState)
{
    if (OldState == nullptr || NewState.rip == 0 || NewState.rsp == 0)
    {
        return;
    }

    ResourceLayerTaskSwitchKernelAsm(OldState, &NewState);
}

/**
 * Function: ResourceLayer::TaskSwitchUser
 * Description: Performs a user-task context switch through assembly helper with address-space switch.
 * Parameters:
 *   CpuState* OldState - Storage for outgoing task state.
 *   const CpuState& NewState - Incoming task state to restore.
 *   const VirtualAddressSpace* NewAddressSpace - Address space metadata for incoming user task.
 * Returns:
 *   void - No return value.
 */
void ResourceLayer::TaskSwitchUser(CpuState* OldState, const CpuState& NewState, const VirtualAddressSpace* NewAddressSpace)
{
    if (OldState == nullptr || NewState.rip == 0 || NewState.rsp == 0 || NewAddressSpace == nullptr)
    {
        return;
    }

    uint64_t PageMapL4TableAddr = NewAddressSpace->GetPageMapL4TableAddr();
    if (PageMapL4TableAddr == 0)
    {
        return;
    }

    ResourceLayerTaskSwitchUserAsm(OldState, &NewState, PageMapL4TableAddr);
}