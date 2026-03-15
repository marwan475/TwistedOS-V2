#include "ResourceLayer.hpp"

#include <CommonUtils.hpp>

extern "C" void ResourceLayerTaskSwitchAsm(CpuState* OldState, const CpuState* NewState);

ResourceLayer::ResourceLayer() : PMM(nullptr), VMM(nullptr), Console(nullptr), KernelHeapVirtualAddrStart(0), KernelHeapVirtualAddrEnd(0), KHM(0, 0), RFS(0, 0)
{
}

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

PhysicalMemoryManager* ResourceLayer::GetPMM() const
{
    return PMM;
}

VirtualMemoryManager* ResourceLayer::GetVMM() const
{
    return VMM;
}

FrameBufferConsole* ResourceLayer::GetConsole() const
{
    return Console;
}

uint64_t ResourceLayer::GetKernelHeapVirtualAddrStart() const
{
    return KernelHeapVirtualAddrStart;
}

uint64_t ResourceLayer::GetKernelHeapVirtualAddrEnd() const
{
    return KernelHeapVirtualAddrEnd;
}

void ResourceLayer::InitializeKernelHeapManager()
{
    KHM = KernelHeapManager(KernelHeapVirtualAddrStart, KernelHeapVirtualAddrEnd);
    Console->printf_("Kernel Heap Manager Initialized\n");
}

void ResourceLayer::InitializeRamFileSystemManager()
{
    RFS = RamFileSystemManager(InitramfsAddress, InitramfsSize);
    Console->printf_("RAM File System Manager Initialized\n");
    RFS.ParseAndPrintInitramfs(Console);
}

void* ResourceLayer::kmalloc(size_t Size)
{
    return KHM.kmalloc(Size);
}

void ResourceLayer::kfree(void* Ptr)
{
    KHM.kfree(Ptr);
}

// Loads file from initramfs into kernel heap
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

    void* AllocatedData = kmalloc(*Size);
    if (AllocatedData == nullptr)
    {
        return nullptr;
    }

    memcpy(AllocatedData, Data, *Size);

    return AllocatedData;
}

void ResourceLayer::TaskSwitch(CpuState* OldState, const CpuState& NewState)
{
    if (OldState == nullptr || NewState.rip == 0 || NewState.rsp == 0)
    {
        return;
    }

    ResourceLayerTaskSwitchAsm(OldState, &NewState);
}