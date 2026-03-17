#include "VirtualAddressSpace.hpp"

#include <Memory/VirtualMemoryManager.hpp>

namespace
{
uint64_t GetRequiredPageCount(uint64_t VirtualAddress, uint64_t Size)
{
    if (Size == 0)
    {
        return 0;
    }

    uint64_t PageOffset = VirtualAddress & (PAGE_SIZE - 1);
    return (PageOffset + Size + PAGE_SIZE - 1) / PAGE_SIZE;
}

bool MapUserRange(VirtualMemoryManager& VMM, uint64_t PhysicalAddress, uint64_t VirtualAddress, uint64_t Size)
{
    uint64_t Pages = GetRequiredPageCount(VirtualAddress, Size);
    if (Pages == 0)
    {
        return true;
    }

    uint64_t MappedPhysicalAddress = PhysicalAddress & PHYS_PAGE_ADDR_MASK;
    uint64_t MappedVirtualAddress  = VirtualAddress & PHYS_PAGE_ADDR_MASK;

    for (uint64_t PageIndex = 0; PageIndex < Pages; ++PageIndex)
    {
        uint64_t PhysicalPage = MappedPhysicalAddress + (PageIndex * PAGE_SIZE);
        uint64_t VirtualPage  = MappedVirtualAddress + (PageIndex * PAGE_SIZE);

        if (!VMM.MapPage(PhysicalPage, VirtualPage, true))
        {
            return false;
        }
    }

    return true;
}
} // namespace

VirtualAddressSpace::VirtualAddressSpace()
    : CodePhysicalAddress(0), CodeSize(0), CodeVirtualAddressStart(0), HeapPhysicalAddress(0), HeapSize(0), HeapVirtualAddressStart(0), StackPhysicalAddress(0), StackSize(0),
      StackVirtualAddressStart(0), PageMapL4TableAddr(0)
{
}

VirtualAddressSpace::VirtualAddressSpace(uint64_t CodePhysicalAddress, uint64_t CodeSize, uint64_t CodeVirtualAddressStart, uint64_t HeapPhysicalAddress, uint64_t HeapSize,
                                         uint64_t HeapVirtualAddressStart, uint64_t StackPhysicalAddress, uint64_t StackSize, uint64_t StackVirtualAddressStart)
    : CodePhysicalAddress(CodePhysicalAddress), CodeSize(CodeSize), CodeVirtualAddressStart(CodeVirtualAddressStart), HeapPhysicalAddress(HeapPhysicalAddress), HeapSize(HeapSize),
      HeapVirtualAddressStart(HeapVirtualAddressStart), StackPhysicalAddress(StackPhysicalAddress), StackSize(StackSize), StackVirtualAddressStart(StackVirtualAddressStart),
      PageMapL4TableAddr(0)
{
}

VirtualAddressSpace::~VirtualAddressSpace()
{
}

void VirtualAddressSpace::SetPageMapL4TableAddr(uint64_t PageMapL4TableAddr)
{
    this->PageMapL4TableAddr = PageMapL4TableAddr;
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
    if (PageMapL4TableAddr == 0)
    {
        return false;
    }

    VirtualMemoryManager VMM(PageMapL4TableAddr, PMM);

    uint64_t CodePages  = (CodeSize + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t HeapPages  = (HeapSize + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t StackPages = (StackSize + PAGE_SIZE - 1) / PAGE_SIZE;

    VMM.MapRange(CodePhysicalAddress, CodeVirtualAddressStart, CodePages, true);
    VMM.MapRange(HeapPhysicalAddress, HeapVirtualAddressStart, HeapPages, true);
    VMM.MapRange(StackPhysicalAddress, StackVirtualAddressStart, StackPages, true);

    SetPageMapL4TableAddr(PageMapL4TableAddr);

    return true;
}

uint64_t VirtualAddressSpace::GetPageMapL4TableAddr() const
{
    return PageMapL4TableAddr;
}

VirtualAddressSpaceELF::VirtualAddressSpaceELF()
    : VirtualAddressSpace(), MemoryRegions{}, MemoryRegionCount(0)
{
}

VirtualAddressSpaceELF::VirtualAddressSpaceELF(uint64_t CodePhysicalAddress, uint64_t CodeSize, uint64_t CodeVirtualAddressStart, uint64_t HeapPhysicalAddress, uint64_t HeapSize,
                                               uint64_t HeapVirtualAddressStart, uint64_t StackPhysicalAddress, uint64_t StackSize, uint64_t StackVirtualAddressStart)
    : VirtualAddressSpace(CodePhysicalAddress, CodeSize, CodeVirtualAddressStart, HeapPhysicalAddress, HeapSize, HeapVirtualAddressStart, StackPhysicalAddress, StackSize,
                          StackVirtualAddressStart),
      MemoryRegions{}, MemoryRegionCount(0)
{
}

VirtualAddressSpaceELF::~VirtualAddressSpaceELF()
{
}

const ELFMemoryRegion* VirtualAddressSpaceELF::GetMemoryRegions() const
{
    return MemoryRegions;
}

size_t VirtualAddressSpaceELF::GetMemoryRegionCount() const
{
    return MemoryRegionCount;
}

bool VirtualAddressSpaceELF::AddMemoryRegion(const ELFMemoryRegion& Region)
{
    if (MemoryRegionCount >= MaxMemoryRegions)
    {
        return false;
    }

    MemoryRegions[MemoryRegionCount++] = Region;
    return true;
}

bool VirtualAddressSpaceELF::Init(uint64_t PageMapL4TableAddr, PhysicalMemoryManager& PMM)
{
    if (PageMapL4TableAddr == 0 || MemoryRegionCount == 0)
    {
        return false;
    }

    VirtualMemoryManager VMM(PageMapL4TableAddr, PMM);

    for (size_t RegionIndex = 0; RegionIndex < MemoryRegionCount; ++RegionIndex)
    {
        const ELFMemoryRegion& Region = MemoryRegions[RegionIndex];
        if (!MapUserRange(VMM, Region.PhysicalAddress, Region.VirtualAddress, Region.Size))
        {
            return false;
        }
    }

    if (!MapUserRange(VMM, GetHeapPhysicalAddress(), GetHeapVirtualAddressStart(), GetHeapSize()) ||
        !MapUserRange(VMM, GetStackPhysicalAddress(), GetStackVirtualAddressStart(), GetStackSize()))
    {
        return false;
    }

    SetPageMapL4TableAddr(PageMapL4TableAddr);

    return true;
}
