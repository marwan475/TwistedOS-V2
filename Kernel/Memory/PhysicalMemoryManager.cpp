/**
 * File: PhysicalMemoryManager.cpp
 * Author: Marwan Mostafa
 * Description: Physical memory map and frame management implementation.
 */

#include <CommonUtils.hpp>
#include <Logging/FrameBufferConsole.hpp>
#include <Memory/PhysicalMemoryManager.hpp>

namespace
{
constexpr uint64_t PMM_BUDDY_PHYSICAL_LIMIT = 0x40000000ULL;
}

/**
 * Function: PhysicalMemoryManager::PhysicalMemoryManager
 * Description: Initializes the physical memory manager state from boot memory map information.
 * Parameters:
 *   MemoryMapInfo MemoryMap - Firmware memory map metadata and pointer.
 *   UINTN NextPageAddress - Next free page address in the current descriptor.
 *   UINTN CurrentDescriptor - Index of the current memory descriptor in use.
 *   UINTN RemainingPagesInDescriptor - Number of pages remaining in the current descriptor.
 * Returns:
 *   PhysicalMemoryManager - Constructed physical memory manager instance.
 */
PhysicalMemoryManager::PhysicalMemoryManager(MemoryMapInfo MemoryMap, UINTN NextPageAddress, UINTN CurrentDescriptor, UINTN RemainingPagesInDescriptor)
    : MemoryMap(MemoryMap), NextPageAddress(NextPageAddress), CurrentDescriptor(CurrentDescriptor), RemainingPagesInDescriptor(RemainingPagesInDescriptor), TotalUsableMemory(0), TotalNumberOfPages(0),
      MemoryDescriptorInfo{0, 0, 0, NULL}, BuddyFreeLists{NULL}, BuddyMaxOrder(0)
{
    for (uint8_t Order = 0; Order < MAX_BUDDY_ORDERS; Order++)
    {
        BuddyFreeLists[Order] = NULL;
    }

    InitializeMemoryStats();
}

/**
 * Function: PhysicalMemoryManager::InitializeMemoryStats
 * Description: Calculates total usable memory and page count from conventional memory descriptors.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void PhysicalMemoryManager::InitializeMemoryStats()
{
    UINTN DescriptorCount = MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize;

    for (UINTN i = 0; i < DescriptorCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR* Desc = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        if (Desc->Type == EfiConventionalMemory)
        {
            TotalUsableMemory += Desc->NumberOfPages * PAGE_SIZE;
            TotalNumberOfPages += Desc->NumberOfPages;
        }
    }
}

uint8_t PhysicalMemoryManager::FloorLog2Pages(uint64_t Pages)
{
    if (Pages <= 1)
    {
        return 0;
    }

    uint8_t Order = 0;
    while ((Order + 1) < MAX_BUDDY_ORDERS && (1ULL << (Order + 1)) <= Pages)
    {
        Order++;
    }

    return Order;
}

uint8_t PhysicalMemoryManager::CeilLog2Pages(uint64_t Pages)
{
    if (Pages <= 1)
    {
        return 0;
    }

    uint8_t Order = FloorLog2Pages(Pages);
    if ((1ULL << Order) < Pages && (Order + 1) < MAX_BUDDY_ORDERS)
    {
        Order++;
    }

    return Order;
}

uint64_t PhysicalMemoryManager::PagesForOrder(uint8_t Order)
{
    return 1ULL << Order;
}

bool PhysicalMemoryManager::RemoveBlockFromFreeList(uint8_t Order, uint64_t BlockAddress)
{
    BuddyBlock* Previous = NULL;
    BuddyBlock* Current  = BuddyFreeLists[Order];

    while (Current != NULL)
    {
        if ((uint64_t) Current == BlockAddress)
        {
            if (Previous == NULL)
            {
                BuddyFreeLists[Order] = Current->Next;
            }
            else
            {
                Previous->Next = Current->Next;
            }

            return true;
        }

        Previous = Current;
        Current  = Current->Next;
    }

    return false;
}

bool PhysicalMemoryManager::IsAddressInManagedRange(uint64_t Address) const
{
    uint64_t BaseAddress = MemoryDescriptorInfo.PhysicalAddressStart;
    uint64_t EndAddress  = BaseAddress + (MemoryDescriptorInfo.TotalNumberOfPages * PAGE_SIZE);
    return Address >= BaseAddress && Address < EndAddress;
}

void PhysicalMemoryManager::InitializeBuddyAllocator()
{
    for (uint8_t Order = 0; Order < MAX_BUDDY_ORDERS; Order++)
    {
        BuddyFreeLists[Order] = NULL;
    }

    BuddyMaxOrder = 0;

    if (MemoryDescriptorInfo.TotalNumberOfPages == 0)
    {
        return;
    }

    BuddyMaxOrder = FloorLog2Pages(MemoryDescriptorInfo.TotalNumberOfPages);

    uint64_t RemainingPages = MemoryDescriptorInfo.TotalNumberOfPages;
    uint64_t CurrentOffset  = 0;
    uint64_t BaseAddress    = MemoryDescriptorInfo.PhysicalAddressStart;

    while (RemainingPages > 0)
    {
        uint8_t Order = FloorLog2Pages(RemainingPages);

        while (Order > 0 && (CurrentOffset % PagesForOrder(Order)) != 0)
        {
            Order--;
        }

        uint64_t    BlockAddress = BaseAddress + (CurrentOffset * PAGE_SIZE);
        BuddyBlock* Block        = (BuddyBlock*) BlockAddress;
        Block->Next              = BuddyFreeLists[Order];
        BuddyFreeLists[Order]    = Block;

        uint64_t BlockPages = PagesForOrder(Order);
        CurrentOffset += BlockPages;
        RemainingPages -= BlockPages;
    }

    MemoryDescriptorInfo.BitMap = (uint8_t*) MemoryDescriptorInfo.PhysicalAddressStart;
}

/**
 * Function: PhysicalMemoryManager::AllocatePagesFromMemoryMap
 * Description: Allocates contiguous pages directly from conventional memory map descriptors.
 * Parameters:
 *   UINTN Pages - Number of pages to allocate.
 * Returns:
 *   void* - Base address of allocated pages, or NULL if no suitable descriptor is found.
 */
void* PhysicalMemoryManager::AllocatePagesFromMemoryMap(UINTN Pages)
{
    if (RemainingPagesInDescriptor < Pages)
    {
        for (UINTN i = CurrentDescriptor + 1; i < MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize; i++)
        {
            EFI_MEMORY_DESCRIPTOR* Desc = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

            if (Desc->Type == EfiConventionalMemory && Desc->NumberOfPages >= Pages)
            {
                CurrentDescriptor          = i;
                RemainingPagesInDescriptor = Desc->NumberOfPages - Pages;
                NextPageAddress            = Desc->PhysicalStart + (Pages * PAGE_SIZE);
                return (void*) Desc->PhysicalStart;
            }
        }

        return NULL;
    }

    RemainingPagesInDescriptor -= Pages;
    void* Page = (void*) NextPageAddress;
    NextPageAddress += Pages * PAGE_SIZE;
    return Page;
}

/**
 * Function: PhysicalMemoryManager::AllocatePagesFromDescriptor
 * Description: Allocates pages from the managed descriptor bitmap using a first-fit contiguous run.
 * Parameters:
 *   UINTN Pages - Number of pages to allocate.
 * Returns:
 *   void* - Base address of the allocated run, or NULL if allocation fails.
 */
void* PhysicalMemoryManager::AllocatePagesFromDescriptor(UINTN Pages)
{
    if (Pages == 0)
        return NULL;

    if (MemoryDescriptorInfo.BitMap == NULL)
        return NULL;

    if (Pages > MemoryDescriptorInfo.NumberOfFreePages)
        return NULL;

    uint8_t RequestedOrder = CeilLog2Pages(Pages);
    if (RequestedOrder > BuddyMaxOrder)
    {
        return NULL;
    }

    uint8_t CurrentOrder = RequestedOrder;
    while (CurrentOrder <= BuddyMaxOrder && BuddyFreeLists[CurrentOrder] == NULL)
    {
        CurrentOrder++;
    }

    if (CurrentOrder > BuddyMaxOrder)
    {
        return NULL;
    }

    BuddyBlock* Block            = BuddyFreeLists[CurrentOrder];
    BuddyFreeLists[CurrentOrder] = Block->Next;
    uint64_t BlockAddress        = (uint64_t) Block;

    while (CurrentOrder > RequestedOrder)
    {
        CurrentOrder--;

        uint64_t    HalfBlockSizeBytes = PagesForOrder(CurrentOrder) * PAGE_SIZE;
        uint64_t    BuddyAddress       = BlockAddress + HalfBlockSizeBytes;
        BuddyBlock* Buddy              = (BuddyBlock*) BuddyAddress;

        Buddy->Next                  = BuddyFreeLists[CurrentOrder];
        BuddyFreeLists[CurrentOrder] = Buddy;
    }

    uint64_t AllocatedPages = PagesForOrder(RequestedOrder);
    if (AllocatedPages > MemoryDescriptorInfo.NumberOfFreePages)
    {
        return NULL;
    }

    MemoryDescriptorInfo.NumberOfFreePages -= AllocatedPages;
    return (void*) BlockAddress;
}

/**
 * Function: PhysicalMemoryManager::FreePagesFromDescriptor
 * Description: Frees pages in the managed descriptor bitmap and updates free-page accounting.
 * Parameters:
 *   void* Address - Base address of the pages to free.
 *   UINTN Pages - Number of pages to free.
 * Returns:
 *   bool - True if inputs were valid and the operation completed, false on invalid input.
 */
bool PhysicalMemoryManager::FreePagesFromDescriptor(void* Address, UINTN Pages)
{
    if (Address == NULL || Pages == 0)
        return false;

    if (MemoryDescriptorInfo.BitMap == NULL)
        return false;

    uint64_t PhysAddr = (uint64_t) Address;
    if (!IsAddressInManagedRange(PhysAddr))
    {
        return false;
    }

    uint64_t BaseAddress = MemoryDescriptorInfo.PhysicalAddressStart;
    if (((PhysAddr - BaseAddress) % PAGE_SIZE) != 0)
    {
        return false;
    }

    uint8_t Order = CeilLog2Pages(Pages);
    if (Order > BuddyMaxOrder)
    {
        return false;
    }

    uint64_t BlockPages = PagesForOrder(Order);
    uint64_t StartPage  = (PhysAddr - BaseAddress) / PAGE_SIZE;
    if (StartPage + BlockPages > MemoryDescriptorInfo.TotalNumberOfPages)
    {
        return false;
    }

    if ((StartPage % BlockPages) != 0)
    {
        return false;
    }

    uint64_t CurrentAddress = PhysAddr;
    uint8_t  CurrentOrder   = Order;

    while (CurrentOrder < BuddyMaxOrder)
    {
        uint64_t CurrentOrderPages = PagesForOrder(CurrentOrder);
        uint64_t CurrentBlockIndex = (CurrentAddress - BaseAddress) / PAGE_SIZE;
        uint64_t BuddyBlockIndex   = CurrentBlockIndex ^ CurrentOrderPages;

        if (BuddyBlockIndex + CurrentOrderPages > MemoryDescriptorInfo.TotalNumberOfPages)
        {
            break;
        }

        uint64_t BuddyAddress = BaseAddress + (BuddyBlockIndex * PAGE_SIZE);
        if (!RemoveBlockFromFreeList(CurrentOrder, BuddyAddress))
        {
            break;
        }

        if (BuddyAddress < CurrentAddress)
        {
            CurrentAddress = BuddyAddress;
        }

        CurrentOrder++;
    }

    BuddyBlock* Block            = (BuddyBlock*) CurrentAddress;
    Block->Next                  = BuddyFreeLists[CurrentOrder];
    BuddyFreeLists[CurrentOrder] = Block;

    MemoryDescriptorInfo.NumberOfFreePages += BlockPages;
    if (MemoryDescriptorInfo.NumberOfFreePages > MemoryDescriptorInfo.TotalNumberOfPages)
    {
        MemoryDescriptorInfo.NumberOfFreePages = MemoryDescriptorInfo.TotalNumberOfPages;
    }

    return true;
}

/**
 * Function: PhysicalMemoryManager::TotalUsableMemoryBytes
 * Description: Returns the total conventional memory size in bytes.
 * Parameters:
 *   None
 * Returns:
 *   UINTN - Total usable memory in bytes.
 */
UINTN PhysicalMemoryManager::TotalUsableMemoryBytes() const
{
    return TotalUsableMemory;
}

/**
 * Function: PhysicalMemoryManager::TotalPages
 * Description: Returns the total number of conventional memory pages.
 * Parameters:
 *   None
 * Returns:
 *   UINTN - Total number of pages.
 */
UINTN PhysicalMemoryManager::TotalPages() const
{
    return TotalNumberOfPages;
}

/**
 * Function: PhysicalMemoryManager::PrintConventionalMemoryMap
 * Description: Prints a summary of conventional memory descriptors to the framebuffer console.
 * Parameters:
 *   FrameBufferConsole& Console - Console used for formatted output.
 * Returns:
 *   void - No return value.
 */
void PhysicalMemoryManager::PrintConventionalMemoryMap(FrameBufferConsole& Console) const
{
    UINTN DescriptorCount             = MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize;
    UINTN ConventionalDescriptorCount = 0;

    for (UINTN i = 0; i < DescriptorCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR* Desc = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        if (Desc->Type == EfiConventionalMemory)
        {
            ConventionalDescriptorCount++;
        }
    }

    Console.printf_("Conventional memory descriptors: %llu\n", (unsigned long long) ConventionalDescriptorCount);

    UINTN ConventionalDescriptorIndex = 0;

    for (UINTN i = 0; i < DescriptorCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR* Desc = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        if (Desc->Type != EfiConventionalMemory)
        {
            continue;
        }

        Console.printf_("Descriptor %llu: %llu pages\n", (unsigned long long) ConventionalDescriptorIndex, (unsigned long long) Desc->NumberOfPages);
        ConventionalDescriptorIndex++;
    }
}

/**
 * Function: PhysicalMemoryManager::PrintMemoryDescriptors
 * Description: Prints summary information for the selected largest managed memory descriptor.
 * Parameters:
 *   FrameBufferConsole& Console - Console used for formatted output.
 * Returns:
 *   void - No return value.
 */
void PhysicalMemoryManager::PrintMemoryDescriptors(FrameBufferConsole& Console) const
{
    if (MemoryDescriptorInfo.TotalNumberOfPages == 0)
    {
        Console.printf_("Memory descriptor not initialized\n");
        return;
    }

    Console.printf_("Largest descriptor: base=0x%llx total=%llu free=%llu buddy=%s maxOrder=%u\n", (unsigned long long) MemoryDescriptorInfo.PhysicalAddressStart,
                    (unsigned long long) MemoryDescriptorInfo.TotalNumberOfPages, (unsigned long long) MemoryDescriptorInfo.NumberOfFreePages, MemoryDescriptorInfo.BitMap ? "yes" : "no",
                    (unsigned int) BuddyMaxOrder);
}

/**
 * Function: PhysicalMemoryManager::InitializeMemoryDescriptors
 * Description: Selects the largest available conventional descriptor and initializes its allocation bitmap.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void PhysicalMemoryManager::InitializeMemoryDescriptors()
{
    uint64_t DescriptorCount = MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize;

    MemoryDescriptorInfo.PhysicalAddressStart = 0;
    MemoryDescriptorInfo.TotalNumberOfPages   = 0;
    MemoryDescriptorInfo.NumberOfFreePages    = 0;
    MemoryDescriptorInfo.BitMap               = NULL;

    for (uint64_t i = 0; i < DescriptorCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR* Desc = (EFI_MEMORY_DESCRIPTOR*) ((uint8_t*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        if (Desc->Type != EfiConventionalMemory)
        {
            continue;
        }

        uint64_t CandidatePhysicalStart = Desc->PhysicalStart;
        uint64_t CandidatePages         = Desc->NumberOfPages;

        if (i < CurrentDescriptor)
        {
            continue;
        }

        if (i == CurrentDescriptor)
        {
            if (RemainingPagesInDescriptor == 0)
            {
                continue;
            }

            CandidatePhysicalStart = NextPageAddress;
            CandidatePages         = RemainingPagesInDescriptor;
        }

        if (CandidatePhysicalStart >= PMM_BUDDY_PHYSICAL_LIMIT)
        {
            continue;
        }

        uint64_t CandidateEnd = CandidatePhysicalStart + (CandidatePages * PAGE_SIZE);
        if (CandidateEnd > PMM_BUDDY_PHYSICAL_LIMIT)
        {
            CandidatePages = (PMM_BUDDY_PHYSICAL_LIMIT - CandidatePhysicalStart) / PAGE_SIZE;
            if (CandidatePages == 0)
            {
                continue;
            }
        }

        if (CandidatePages > MemoryDescriptorInfo.TotalNumberOfPages)
        {
            MemoryDescriptorInfo.PhysicalAddressStart = CandidatePhysicalStart;
            MemoryDescriptorInfo.TotalNumberOfPages   = CandidatePages;
            MemoryDescriptorInfo.NumberOfFreePages    = CandidatePages;
            MemoryDescriptorInfo.BitMap               = NULL;
        }
    }

    InitializeBuddyAllocator();
}
