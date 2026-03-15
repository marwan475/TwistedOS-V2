#include "VirtualAddressSpace.hpp"

#include <Memory/VirtualMemoryManager.hpp>

VirtualAddressSpace::VirtualAddressSpace()
    : CodePhysicalAddress(0), CodeSize(0), CodeVirtualAddressStart(0), HeapPhysicalAddress(0), HeapSize(0), HeapVirtualAddressStart(0), StackPhysicalAddress(0), StackSize(0),
      StackVirtualAddressStart(0)
{
}

VirtualAddressSpace::VirtualAddressSpace(uint64_t CodePhysicalAddress, uint64_t CodeSize, uint64_t CodeVirtualAddressStart, uint64_t HeapPhysicalAddress, uint64_t HeapSize,
                                         uint64_t HeapVirtualAddressStart, uint64_t StackPhysicalAddress, uint64_t StackSize, uint64_t StackVirtualAddressStart)
    : CodePhysicalAddress(CodePhysicalAddress), CodeSize(CodeSize), CodeVirtualAddressStart(CodeVirtualAddressStart), HeapPhysicalAddress(HeapPhysicalAddress), HeapSize(HeapSize),
      HeapVirtualAddressStart(HeapVirtualAddressStart), StackPhysicalAddress(StackPhysicalAddress), StackSize(StackSize), StackVirtualAddressStart(StackVirtualAddressStart)
{
}

uint64_t VirtualAddressSpace::GetCodePhysicalAddress() const
{
    return CodePhysicalAddress;
}

uint64_t VirtualAddressSpace::GetCodeSize() const
{
    return CodeSize;
}

uint64_t VirtualAddressSpace::GetCodeVirtualAddressStart() const
{
    return CodeVirtualAddressStart;
}

uint64_t VirtualAddressSpace::GetHeapPhysicalAddress() const
{
    return HeapPhysicalAddress;
}

uint64_t VirtualAddressSpace::GetHeapSize() const
{
    return HeapSize;
}

uint64_t VirtualAddressSpace::GetHeapVirtualAddressStart() const
{
    return HeapVirtualAddressStart;
}

uint64_t VirtualAddressSpace::GetStackPhysicalAddress() const
{
    return StackPhysicalAddress;
}

uint64_t VirtualAddressSpace::GetStackSize() const
{
    return StackSize;
}

uint64_t VirtualAddressSpace::GetStackTop() const
{
    return StackVirtualAddressStart + StackSize;
}

uint64_t VirtualAddressSpace::GetStackVirtualAddressStart() const
{
    return StackVirtualAddressStart;
}

bool VirtualAddressSpace::Init(uint64_t PageMapL4TableAddr, PhysicalMemoryManager& PMM)
{
    VirtualMemoryManager VMM(PageMapL4TableAddr, PMM);

    uint64_t CodePages  = (CodeSize + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t HeapPages  = (HeapSize + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t StackPages = (StackSize + PAGE_SIZE - 1) / PAGE_SIZE;

    VMM.MapRange(CodePhysicalAddress, CodeVirtualAddressStart, CodePages);
    VMM.MapRange(HeapPhysicalAddress, HeapVirtualAddressStart, HeapPages);
    VMM.MapRange(StackPhysicalAddress, StackVirtualAddressStart, StackPages);

    this->PageMapL4TableAddr = PageMapL4TableAddr;

    return true;
}

uint64_t VirtualAddressSpace::GetPageMapL4TableAddr() const
{
    return PageMapL4TableAddr;
}
