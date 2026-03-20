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
ResourceLayer::ResourceLayer()
    : PMM(nullptr), VMM(nullptr), Console(nullptr), KernelHeapVirtualAddrStart(0), KernelHeapVirtualAddrEnd(0), KernelPageMapL4TableAddr(0), InitramfsAddress(0), InitramfsSize(0), KHM(0, 0),
      RFS(0, 0), EFSManager(nullptr), Terminal(nullptr), InputKeyboard(nullptr), DevManager(nullptr)
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
    this->KernelPageMapL4TableAddr   = ReadCurrentPageTable();
}

void ResourceLayer::InitializeFrameBuffer(const EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE& GopMode)
{
    FB.Initialize(GopMode);
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

FrameBuffer* ResourceLayer::GetFrameBuffer()
{
    return &FB;
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

TTY* ResourceLayer::GetTTY() const
{
    return Terminal;
}

Keyboard* ResourceLayer::GetKeyboard() const
{
    return InputKeyboard;
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

void ResourceLayer::InitializeTTY()
{
    if (Terminal != nullptr)
    {
        delete Terminal;
        Terminal = nullptr;
    }

    uint32_t InitialCursorX = 0;
    uint32_t InitialCursorY = 0;

    if (Console != nullptr)
    {
        InitialCursorX = Console->GetCursorX();
        InitialCursorY = Console->GetCursorY();
    }

    Terminal = new TTY(&FB, InitialCursorX, InitialCursorY);

    if (InputKeyboard != nullptr)
    {
        InputKeyboard->SetTTY(Terminal);
    }

    if (Terminal != nullptr)
    {
        Terminal->printf_("TTY Initialized\n");
    }
}

void ResourceLayer::InitializeKeyboard()
{
    if (InputKeyboard != nullptr)
    {
        delete InputKeyboard;
        InputKeyboard = nullptr;
    }

    InputKeyboard = new Keyboard();
    InputKeyboard->Initialize(Terminal);
    Console->printf_("Keyboard Initialized\n");
}

void ResourceLayer::InitializeDeviceManager()
{
    if (DevManager != nullptr)
    {
        delete DevManager;
        DevManager = nullptr;
    }

    DevManager = new DeviceManager();
    DevManager->Initialize(Terminal);
    DevManager->PrintPCI(Terminal);
}

void ResourceLayer::InitPartitionManager()
{
    if (DevManager == nullptr)
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition manager init failed: device manager unavailable\n");
        }
        return;
    }

    if (!PartManager.RefreshPartitionCache(DevManager) && Terminal != nullptr)
    {
        Terminal->printf_("partition manager init: partition cache refresh failed\n");
    }
}

bool ResourceLayer::InitializeRootFileSystemManager(const RootFileSystemPartitionInfo* PartitionInfo)
{
    if (EFSManager != nullptr)
    {
        delete EFSManager;
        EFSManager = nullptr;
    }

    if (DevManager == nullptr)
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("root filesystem manager init failed: device manager unavailable\n");
        }
        return false;
    }

    if (PartitionInfo == nullptr)
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("root filesystem manager init failed: invalid partition info\n");
        }
        return false;
    }

    IDEController* Controller = DevManager->GetPrimaryIDEController();
    if (Controller == nullptr)
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("root filesystem controller not available\n");
        }
        return false;
    }

    EFSManager = new ExtendedFileSystemManager(Controller);
    if (!EFSManager->ConfigurePartition(PartitionInfo->StartLBA, PartitionInfo->SectorCount))
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("root filesystem manager init failed: configure partition failed device=%s start_lba=%lu sectors=%lu\n",
                              (PartitionInfo->DevicePath[0] != '\0') ? PartitionInfo->DevicePath : "<none>", static_cast<unsigned long>(PartitionInfo->StartLBA),
                              static_cast<unsigned long>(PartitionInfo->SectorCount));
        }

        delete EFSManager;
        EFSManager = nullptr;
        return false;
    }

    if (!EFSManager->Initialize())
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("root filesystem manager init failed: ext2 initialize failed device=%s start_lba=%lu sectors=%lu\n",
                              (PartitionInfo->DevicePath[0] != '\0') ? PartitionInfo->DevicePath : "<none>", static_cast<unsigned long>(PartitionInfo->StartLBA),
                              static_cast<unsigned long>(PartitionInfo->SectorCount));
        }

        delete EFSManager;
        EFSManager = nullptr;
        return false;
    }

    if (Terminal != nullptr)
    {
        Terminal->printf_("root filesystem found: device=%s partition=%u start_lba=%lu sectors=%lu ext2_block=%u\n",
                          (PartitionInfo->DevicePath[0] != '\0') ? PartitionInfo->DevicePath : "<none>", PartitionInfo->PartitionIndex,
                          static_cast<unsigned long>(PartitionInfo->StartLBA), static_cast<unsigned long>(PartitionInfo->SectorCount), EFSManager->GetBlockSizeBytes());
        EFSManager->PrintFileSystem(Terminal);
        EFSManager->PrintFileTree(Terminal);
    }

    return true;
}

bool ResourceLayer::LocateRootFileSystemPartition(RootFileSystemPartitionInfo* PartitionInfo)
{
    return PartManager.LocateRootFileSystemPartition(DevManager, PartitionInfo);
}

PartitionManager* ResourceLayer::GetPartitionManager()
{
    return &PartManager;
}

DeviceManager* ResourceLayer::GetDeviceManager() const
{
    return DevManager;
}

ExtendedFileSystemManager* ResourceLayer::GetExtendedFileSystemManager() const
{
    return EFSManager;
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

uint64_t ResourceLayer::ReadCurrentPageTable() const
{
    uint64_t PageTableAddress = 0;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(PageTableAddress));
    return PageTableAddress;
}

void ResourceLayer::LoadPageTable(uint64_t PageMapL4TableAddr)
{
    if (PageMapL4TableAddr == 0)
    {
        return;
    }

    __asm__ __volatile__("mov %0, %%cr3" : : "r"(PageMapL4TableAddr) : "memory");
}

void ResourceLayer::LoadKernelPageTable()
{
    if (KernelPageMapL4TableAddr == 0)
    {
        return;
    }

    LoadPageTable(KernelPageMapL4TableAddr);
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

    if (KernelPageMapL4TableAddr != 0)
    {
        LoadPageTable(KernelPageMapL4TableAddr);
    }

    ResourceLayerTaskSwitchKernelAsm(OldState, &NewState);
}

void ResourceLayer::TaskSwitchKernelCurrentAddressSpace(CpuState* OldState, const CpuState& NewState)
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