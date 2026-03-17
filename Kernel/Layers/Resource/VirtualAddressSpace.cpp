/**
 * File: VirtualAddressSpace.cpp
 * Author: Marwan Mostafa
 * Description: Virtual address space management implementation.
 */

#include "VirtualAddressSpace.hpp"

#include <Memory/VirtualMemoryManager.hpp>

namespace
{
/**
 * Function: GetRequiredPageCount
 * Description: Computes how many pages are needed to cover a virtual address range.
 * Parameters:
 *   uint64_t VirtualAddress - Start virtual address.
 *   uint64_t Size - Range size in bytes.
 * Returns:
 *   uint64_t - Number of pages required for the range.
 */
uint64_t GetRequiredPageCount(uint64_t VirtualAddress, uint64_t Size)
{
    if (Size == 0)
    {
        return 0;
    }

    uint64_t PageOffset = VirtualAddress & (PAGE_SIZE - 1);
    return (PageOffset + Size + PAGE_SIZE - 1) / PAGE_SIZE;
}

/**
 * Function: MapUserRange
 * Description: Maps a physical range into user virtual space page-by-page.
 * Parameters:
 *   VirtualMemoryManager& VMM - Virtual memory manager used for mappings.
 *   uint64_t PhysicalAddress - Start physical address.
 *   uint64_t VirtualAddress - Start virtual address.
 *   uint64_t Size - Number of bytes to map.
 *   bool Writable - Whether mapped pages are writable.
 * Returns:
 *   bool - True on success, false if any page mapping fails.
 */
bool MapUserRange(VirtualMemoryManager& VMM, uint64_t PhysicalAddress, uint64_t VirtualAddress, uint64_t Size, bool Writable)
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

        if (!VMM.MapPage(PhysicalPage, VirtualPage, PageMappingFlags(true, Writable)))
        {
            return false;
        }
    }

    return true;
}
} // namespace

/**
 * Function: VirtualAddressSpace::VirtualAddressSpace
 * Description: Constructs an empty virtual address space descriptor.
 * Parameters:
 *   None
 * Returns:
 *   VirtualAddressSpace - Constructed virtual address space instance.
 */
VirtualAddressSpace::VirtualAddressSpace()
    : CodePhysicalAddress(0), CodeSize(0), CodeVirtualAddressStart(0), HeapPhysicalAddress(0), HeapSize(0), HeapVirtualAddressStart(0), StackPhysicalAddress(0), StackSize(0),
      StackVirtualAddressStart(0), PageMapL4TableAddr(0)
{
}

/**
 * Function: VirtualAddressSpace::VirtualAddressSpace
 * Description: Constructs a virtual address space descriptor with code, heap, and stack mappings.
 * Parameters:
 *   uint64_t CodePhysicalAddress - Physical base address of code.
 *   uint64_t CodeSize - Size of code region in bytes.
 *   uint64_t CodeVirtualAddressStart - Virtual base address of code.
 *   uint64_t HeapPhysicalAddress - Physical base address of heap.
 *   uint64_t HeapSize - Size of heap region in bytes.
 *   uint64_t HeapVirtualAddressStart - Virtual base address of heap.
 *   uint64_t StackPhysicalAddress - Physical base address of stack.
 *   uint64_t StackSize - Size of stack region in bytes.
 *   uint64_t StackVirtualAddressStart - Virtual base address of stack.
 * Returns:
 *   VirtualAddressSpace - Constructed virtual address space instance.
 */
VirtualAddressSpace::VirtualAddressSpace(uint64_t CodePhysicalAddress, uint64_t CodeSize, uint64_t CodeVirtualAddressStart, uint64_t HeapPhysicalAddress, uint64_t HeapSize,
                                         uint64_t HeapVirtualAddressStart, uint64_t StackPhysicalAddress, uint64_t StackSize, uint64_t StackVirtualAddressStart)
    : CodePhysicalAddress(CodePhysicalAddress), CodeSize(CodeSize), CodeVirtualAddressStart(CodeVirtualAddressStart), HeapPhysicalAddress(HeapPhysicalAddress), HeapSize(HeapSize),
      HeapVirtualAddressStart(HeapVirtualAddressStart), StackPhysicalAddress(StackPhysicalAddress), StackSize(StackSize), StackVirtualAddressStart(StackVirtualAddressStart), PageMapL4TableAddr(0)
{
}

/**
 * Function: VirtualAddressSpace::~VirtualAddressSpace
 * Description: Destroys a virtual address space descriptor.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
VirtualAddressSpace::~VirtualAddressSpace()
{
}

/**
 * Function: VirtualAddressSpace::SetPageMapL4TableAddr
 * Description: Sets the PML4 table address associated with this address space.
 * Parameters:
 *   uint64_t PageMapL4TableAddr - Physical address of PML4 table.
 * Returns:
 *   void - No return value.
 */
void VirtualAddressSpace::SetPageMapL4TableAddr(uint64_t PageMapL4TableAddr)
{
    this->PageMapL4TableAddr = PageMapL4TableAddr;
}

/**
 * Function: VirtualAddressSpace::GetCodePhysicalAddress
 * Description: Returns the code segment physical address.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - Code physical base address.
 */
uint64_t VirtualAddressSpace::GetCodePhysicalAddress() const
{
    return CodePhysicalAddress;
}

/**
 * Function: VirtualAddressSpace::GetCodeSize
 * Description: Returns code segment size.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - Code size in bytes.
 */
uint64_t VirtualAddressSpace::GetCodeSize() const
{
    return CodeSize;
}

/**
 * Function: VirtualAddressSpace::GetCodeVirtualAddressStart
 * Description: Returns code segment virtual base address.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - Code virtual base address.
 */
uint64_t VirtualAddressSpace::GetCodeVirtualAddressStart() const
{
    return CodeVirtualAddressStart;
}

/**
 * Function: VirtualAddressSpace::GetHeapPhysicalAddress
 * Description: Returns heap segment physical address.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - Heap physical base address.
 */
uint64_t VirtualAddressSpace::GetHeapPhysicalAddress() const
{
    return HeapPhysicalAddress;
}

/**
 * Function: VirtualAddressSpace::GetHeapSize
 * Description: Returns heap segment size.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - Heap size in bytes.
 */
uint64_t VirtualAddressSpace::GetHeapSize() const
{
    return HeapSize;
}

/**
 * Function: VirtualAddressSpace::GetHeapVirtualAddressStart
 * Description: Returns heap segment virtual base address.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - Heap virtual base address.
 */
uint64_t VirtualAddressSpace::GetHeapVirtualAddressStart() const
{
    return HeapVirtualAddressStart;
}

/**
 * Function: VirtualAddressSpace::GetStackPhysicalAddress
 * Description: Returns stack segment physical address.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - Stack physical base address.
 */
uint64_t VirtualAddressSpace::GetStackPhysicalAddress() const
{
    return StackPhysicalAddress;
}

/**
 * Function: VirtualAddressSpace::GetStackSize
 * Description: Returns stack segment size.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - Stack size in bytes.
 */
uint64_t VirtualAddressSpace::GetStackSize() const
{
    return StackSize;
}

/**
 * Function: VirtualAddressSpace::GetStackTop
 * Description: Returns top virtual address of the stack region.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - Stack top virtual address.
 */
uint64_t VirtualAddressSpace::GetStackTop() const
{
    return StackVirtualAddressStart + StackSize;
}

/**
 * Function: VirtualAddressSpace::GetStackVirtualAddressStart
 * Description: Returns stack segment virtual base address.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - Stack virtual base address.
 */
uint64_t VirtualAddressSpace::GetStackVirtualAddressStart() const
{
    return StackVirtualAddressStart;
}

/**
 * Function: VirtualAddressSpace::Init
 * Description: Maps configured code, heap, and stack regions into the provided page table.
 * Parameters:
 *   uint64_t PageMapL4TableAddr - Physical address of PML4 table to populate.
 *   PhysicalMemoryManager& PMM - Physical memory manager used by VMM for page-table allocation.
 * Returns:
 *   bool - True if initialization succeeds, false on invalid input.
 */
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

    MapUserRange(VMM, CodePhysicalAddress, CodeVirtualAddressStart, CodePages * PAGE_SIZE, true);
    MapUserRange(VMM, HeapPhysicalAddress, HeapVirtualAddressStart, HeapPages * PAGE_SIZE, true);
    MapUserRange(VMM, StackPhysicalAddress, StackVirtualAddressStart, StackPages * PAGE_SIZE, true);

    SetPageMapL4TableAddr(PageMapL4TableAddr);

    return true;
}

/**
 * Function: VirtualAddressSpace::GetPageMapL4TableAddr
 * Description: Returns the page-map level-4 table physical address.
 * Parameters:
 *   None
 * Returns:
 *   uint64_t - PML4 physical address.
 */
uint64_t VirtualAddressSpace::GetPageMapL4TableAddr() const
{
    return PageMapL4TableAddr;
}

/**
 * Function: VirtualAddressSpaceELF::VirtualAddressSpaceELF
 * Description: Constructs an empty ELF virtual address space descriptor.
 * Parameters:
 *   None
 * Returns:
 *   VirtualAddressSpaceELF - Constructed ELF virtual address space instance.
 */
VirtualAddressSpaceELF::VirtualAddressSpaceELF() : VirtualAddressSpace(), MemoryRegions{}, MemoryRegionCount(0)
{
}

/**
 * Function: VirtualAddressSpaceELF::VirtualAddressSpaceELF
 * Description: Constructs an ELF virtual address space descriptor with base code/heap/stack metadata.
 * Parameters:
 *   uint64_t CodePhysicalAddress - Physical base address of code.
 *   uint64_t CodeSize - Size of code region in bytes.
 *   uint64_t CodeVirtualAddressStart - Virtual base address of code.
 *   uint64_t HeapPhysicalAddress - Physical base address of heap.
 *   uint64_t HeapSize - Size of heap region in bytes.
 *   uint64_t HeapVirtualAddressStart - Virtual base address of heap.
 *   uint64_t StackPhysicalAddress - Physical base address of stack.
 *   uint64_t StackSize - Size of stack region in bytes.
 *   uint64_t StackVirtualAddressStart - Virtual base address of stack.
 * Returns:
 *   VirtualAddressSpaceELF - Constructed ELF virtual address space instance.
 */
VirtualAddressSpaceELF::VirtualAddressSpaceELF(uint64_t CodePhysicalAddress, uint64_t CodeSize, uint64_t CodeVirtualAddressStart, uint64_t HeapPhysicalAddress, uint64_t HeapSize,
                                               uint64_t HeapVirtualAddressStart, uint64_t StackPhysicalAddress, uint64_t StackSize, uint64_t StackVirtualAddressStart)
    : VirtualAddressSpace(CodePhysicalAddress, CodeSize, CodeVirtualAddressStart, HeapPhysicalAddress, HeapSize, HeapVirtualAddressStart, StackPhysicalAddress, StackSize, StackVirtualAddressStart),
      MemoryRegions{}, MemoryRegionCount(0)
{
}

/**
 * Function: VirtualAddressSpaceELF::~VirtualAddressSpaceELF
 * Description: Destroys an ELF virtual address space descriptor.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
VirtualAddressSpaceELF::~VirtualAddressSpaceELF()
{
}

/**
 * Function: VirtualAddressSpaceELF::GetMemoryRegions
 * Description: Returns pointer to stored ELF memory region table.
 * Parameters:
 *   None
 * Returns:
 *   const ELFMemoryRegion* - Pointer to memory region array.
 */
const ELFMemoryRegion* VirtualAddressSpaceELF::GetMemoryRegions() const
{
    return MemoryRegions;
}

/**
 * Function: VirtualAddressSpaceELF::GetMemoryRegionCount
 * Description: Returns number of populated ELF memory regions.
 * Parameters:
 *   None
 * Returns:
 *   size_t - Number of memory regions.
 */
size_t VirtualAddressSpaceELF::GetMemoryRegionCount() const
{
    return MemoryRegionCount;
}

/**
 * Function: VirtualAddressSpaceELF::AddMemoryRegion
 * Description: Appends an ELF memory region to the internal region table.
 * Parameters:
 *   const ELFMemoryRegion& Region - Region metadata to add.
 * Returns:
 *   bool - True on success, false if capacity is exhausted.
 */
bool VirtualAddressSpaceELF::AddMemoryRegion(const ELFMemoryRegion& Region)
{
    if (MemoryRegionCount >= MaxMemoryRegions)
    {
        return false;
    }

    MemoryRegions[MemoryRegionCount++] = Region;
    return true;
}

/**
 * Function: VirtualAddressSpaceELF::Init
 * Description: Maps all ELF regions plus heap and stack into the provided page table.
 * Parameters:
 *   uint64_t PageMapL4TableAddr - Physical address of PML4 table to populate.
 *   PhysicalMemoryManager& PMM - Physical memory manager used by VMM for page-table allocation.
 * Returns:
 *   bool - True if all mappings succeed, false otherwise.
 */
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
        if (!MapUserRange(VMM, Region.PhysicalAddress, Region.VirtualAddress, Region.Size, Region.Writable))
        {
            return false;
        }
    }

    if (!MapUserRange(VMM, GetHeapPhysicalAddress(), GetHeapVirtualAddressStart(), GetHeapSize(), true)
        || !MapUserRange(VMM, GetStackPhysicalAddress(), GetStackVirtualAddressStart(), GetStackSize(), true))
    {
        return false;
    }

    SetPageMapL4TableAddr(PageMapL4TableAddr);

    return true;
}
