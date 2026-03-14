#include "ResourceLayer.hpp"

extern "C" void ResourceLayerTaskSwitchAsm(CpuState* OldState, void** OldStack, const CpuState* NewState,
                                             void* NewStack);

ResourceLayer::ResourceLayer()
    : PMM(nullptr), VMM(nullptr), Console(nullptr), KernelHeapVirtualAddrStart(0), KernelHeapVirtualAddrEnd(0),
      KHM(0, 0)
{
}

void ResourceLayer::Initialize(PhysicalMemoryManager* PMM, VirtualMemoryManager* VMM, FrameBufferConsole* Console,
                               uint64_t KernelHeapVirtualAddrStart, uint64_t KernelHeapVirtualAddrEnd)
{
    this->PMM                        = PMM;
    this->VMM                        = VMM;
    this->Console                    = Console;
    this->KernelHeapVirtualAddrStart = KernelHeapVirtualAddrStart;
    this->KernelHeapVirtualAddrEnd   = KernelHeapVirtualAddrEnd;
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
}

void* ResourceLayer::kmalloc(size_t Size)
{
    return KHM.kmalloc(Size);
}

void ResourceLayer::kfree(void* Ptr)
{
    KHM.kfree(Ptr);
}

void ResourceLayer::TaskSwitch(CpuState* OldState, void** OldStack, const CpuState& NewState, void* NewStack)
{
    if (OldState == nullptr || OldStack == nullptr || NewStack == nullptr || NewState.rip == 0)
    {
        return;
    }

    ResourceLayerTaskSwitchAsm(OldState, OldStack, &NewState, NewStack);
}