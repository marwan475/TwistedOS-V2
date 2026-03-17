/**
 * File: MemoryManager.cpp
 * Author: Marwan Mostafa
 * Description: Bootloader physical memory map and allocation logic.
 */

#include <CommonUtils.hpp>
#include <MemoryManager.hpp>

// Should only be created after ExitBootServices

/**
 * Function: MemoryManager::MemoryManager
 * Description: Initializes memory manager state and allocates/clears the top-level page table.
 * Parameters:
 *   MemoryMapInfo MemoryMap - UEFI memory map metadata and descriptor buffer used for allocations.
 * Returns:
 *   N/A - Constructor initializes the MemoryManager object.
 */
MemoryManager::MemoryManager(MemoryMapInfo MemoryMap) : MemoryMap(MemoryMap)
{
    NextPageAddress            = NULL;
    CurrentDescriptor          = 0;
    RemainingPagesInDescriptor = 0;

    PageMapL4Table = (PageTableEntry*) AllocateAvailablePagesFromMemoryMap(1);
    kmemset((void*) PageMapL4Table, 0, PAGE_SIZE * 1);
}

/**
 * Function: MemoryManager::~MemoryManager
 * Description: Destroys the MemoryManager object.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   N/A - Destructor does not return a value.
 */
MemoryManager::~MemoryManager()
{
}

/**
 * Function: MemoryManager::GetPageMapL4Table
 * Description: Returns the physical address of the PML4 table.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   UINTN - Address of the level-4 page map table.
 */
UINTN MemoryManager::GetPageMapL4Table() const
{
    return (UINTN) PageMapL4Table;
}

/**
 * Function: MemoryManager::GetNextPageAddress
 * Description: Returns the next available page allocation address tracked by the manager.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   UINTN - Address of the next page candidate.
 */
UINTN MemoryManager::GetNextPageAddress() const
{
    return (UINTN) NextPageAddress;
}

/**
 * Function: MemoryManager::GetCurrentDescriptor
 * Description: Returns the current memory descriptor index used for page allocation.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   UINTN - Current descriptor index.
 */
UINTN MemoryManager::GetCurrentDescriptor() const
{
    return CurrentDescriptor;
}

/**
 * Function: MemoryManager::GetRemainingPagesInDescriptor
 * Description: Returns the number of pages still available in the active memory descriptor.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   UINTN - Remaining allocatable pages in the current descriptor.
 */
UINTN MemoryManager::GetRemainingPagesInDescriptor() const
{
    return RemainingPagesInDescriptor;
}

/**
 * Function: MemoryManager::AllocateAvailablePagesFromMemoryMap
 * Description: Allocates contiguous pages from conventional memory regions tracked in the memory map.
 * Parameters:
 *   UINTN Pages - Number of pages requested.
 * Returns:
 *   void* - Base address of allocated pages, or NULL if no suitable region is found.
 */
void* MemoryManager::AllocateAvailablePagesFromMemoryMap(UINTN Pages)
{
    if (RemainingPagesInDescriptor < Pages)
    {
        for (UINTN i = CurrentDescriptor + 1; i < MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize; i++)
        {
            EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

            if (desc->Type == EfiConventionalMemory && desc->NumberOfPages >= Pages)
            {
                CurrentDescriptor          = i;
                RemainingPagesInDescriptor = desc->NumberOfPages - Pages;
                NextPageAddress            = (void*) (desc->PhysicalStart + (Pages * PAGE_SIZE));
                return (void*) desc->PhysicalStart;
            }

            if (i >= MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize)
            {
                return NULL;
            }
        }
    }

    RemainingPagesInDescriptor -= Pages;
    void* Page      = NextPageAddress;
    NextPageAddress = (void*) ((UINT8*) Page + (Pages * PAGE_SIZE));
    return Page;
}

/**
 * Function: MemoryManager::MapPage
 * Description: Creates page table entries to map one physical page to a virtual page.
 * Parameters:
 *   UINTN PhysicalAddr - Physical page-aligned address to map from.
 *   UINTN VirtualAddr - Virtual page-aligned address to map to.
 * Returns:
 *   bool - true if mapping path completed.
 */
bool MemoryManager::MapPage(UINTN PhysicalAddr, UINTN VirtualAddr)
{
    VirtualAddress Vaddr;
    Vaddr.value = VirtualAddr;

    UINTN PageMapL4TableIndex            = Vaddr.fields.pml4_index;
    UINTN PageDirectoryPointerTableIndex = Vaddr.fields.pdpt_index;
    UINTN PageDirectoryTableIndex        = Vaddr.fields.pd_index;
    UINTN PageTableIndex                 = Vaddr.fields.pt_index;

    PageTableEntry PmL4Entry = PageMapL4Table[PageMapL4TableIndex];

    // If No PageDirectoryPointerTable
    if (!PmL4Entry.fields.present)
    {
        void*          PageDirectoryPointerTableAddr = AllocateAvailablePagesFromMemoryMap(1);
        PageTableEntry PageDirectoryPointerTable     = {0};
        PageDirectoryPointerTable.value              = (UINTN) PageDirectoryPointerTableAddr;

        kmemset(PageDirectoryPointerTableAddr, 0, PAGE_SIZE);

        PageDirectoryPointerTable.fields.present     = 1;
        PageDirectoryPointerTable.fields.writeable   = 1;
        PageDirectoryPointerTable.fields.user_access = 0;

        PageMapL4Table[PageMapL4TableIndex] = PageDirectoryPointerTable;
        PmL4Entry                           = PageMapL4Table[PageMapL4TableIndex];
    }

    PageTableEntry* PageDirectoryPointerTable = (PageTableEntry*) (PmL4Entry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PDPTEntry                 = PageDirectoryPointerTable[PageDirectoryPointerTableIndex];

    // If No PageDirectoryTable
    if (!PDPTEntry.fields.present)
    {
        void*          PageDirectoryTableAddr = AllocateAvailablePagesFromMemoryMap(1);
        PageTableEntry PageDirectoryTable     = {0};
        PageDirectoryTable.value              = (UINTN) PageDirectoryTableAddr;

        kmemset(PageDirectoryTableAddr, 0, PAGE_SIZE);

        PageDirectoryTable.fields.present     = 1;
        PageDirectoryTable.fields.writeable   = 1;
        PageDirectoryTable.fields.user_access = 0;

        PageDirectoryPointerTable[PageDirectoryPointerTableIndex] = PageDirectoryTable;
        PDPTEntry                                                 = PageDirectoryPointerTable[PageDirectoryPointerTableIndex];
    }

    PageTableEntry* PageDirectoryTable = (PageTableEntry*) (PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PDTEntry           = PageDirectoryTable[PageDirectoryTableIndex];

    // If No PageTable
    if (!PDTEntry.fields.present)
    {
        void*          PageTableAddr = AllocateAvailablePagesFromMemoryMap(1);
        PageTableEntry PageTable     = {0};
        PageTable.value              = (UINTN) PageTableAddr;

        kmemset(PageTableAddr, 0, PAGE_SIZE);

        PageTable.fields.present     = 1;
        PageTable.fields.writeable   = 1;
        PageTable.fields.user_access = 0;

        PageDirectoryTable[PageDirectoryTableIndex] = PageTable;
        PDTEntry                                    = PageDirectoryTable[PageDirectoryTableIndex];
    }

    PageTableEntry* PageTable = (PageTableEntry*) (PDTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PTEntry   = PageTable[PageTableIndex];

    // If no Entry in page table
    if (!PTEntry.fields.present)
    {
        PageTableEntry NewPTEntry = {0};
        NewPTEntry.value          = PhysicalAddr & PHYS_PAGE_ADDR_MASK;

        NewPTEntry.fields.present     = 1;
        NewPTEntry.fields.writeable   = 1;
        NewPTEntry.fields.user_access = 0;

        PageTable[PageTableIndex] = NewPTEntry;
    }

    return true;
}

/**
 * Function: MemoryManager::UnmapPage
 * Description: Clears the page table entry for a virtual page and flushes that page from the TLB.
 * Parameters:
 *   UINTN VirtualAddr - Virtual address of the page to unmap.
 * Returns:
 *   bool - true if an existing mapping path was found and cleared; otherwise false.
 */
bool MemoryManager::UnmapPage(UINTN VirtualAddr)
{
    VirtualAddress Vaddr;
    Vaddr.value = VirtualAddr;

    UINTN PageMapL4TableIndex            = Vaddr.fields.pml4_index;
    UINTN PageDirectoryPointerTableIndex = Vaddr.fields.pdpt_index;
    UINTN PageDirectoryTableIndex        = Vaddr.fields.pd_index;
    UINTN PageTableIndex                 = Vaddr.fields.pt_index;

    PageTableEntry PmL4Entry = PageMapL4Table[PageMapL4TableIndex];

    if (!PmL4Entry.fields.present)
        return false;

    PageTableEntry* PageDirectoryPointerTable = (PageTableEntry*) (PmL4Entry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PDPTEntry                 = PageDirectoryPointerTable[PageDirectoryPointerTableIndex];

    if (!PDPTEntry.fields.present)
        return false;

    PageTableEntry* PageDirectoryTable = (PageTableEntry*) (PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PDTEntry           = PageDirectoryTable[PageDirectoryTableIndex];

    if (!PDTEntry.fields.present)
        return false;

    PageTableEntry* PageTable = (PageTableEntry*) (PDTEntry.value & PHYS_PAGE_ADDR_MASK);

    PageTable[PageTableIndex].value = 0;

    // Flush the TLB cache for this page
    __asm__("invlpg (%0)\n" : : "r"(VirtualAddr));

    return true;
}

/**
 * Function: MemoryManager::IdentityMapPage
 * Description: Maps a page so its virtual address equals its physical address.
 * Parameters:
 *   UINTN Addr - Page address to identity-map.
 * Returns:
 *   bool - Result from MapPage for the identity mapping operation.
 */
bool MemoryManager::IdentityMapPage(UINTN Addr)
{
    return MapPage(Addr, Addr);
}

/**
 * Function: MemoryManager::IdentityMapRange
 * Description: Identity-maps an address range, rounded to page boundaries.
 * Parameters:
 *   UINTN BaseAddr - Start address of the range.
 *   UINTN Size - Size of the range in bytes.
 * Returns:
 *   UINTN - End address (exclusive) of the mapped page-aligned range.
 */
UINTN MemoryManager::IdentityMapRange(UINTN BaseAddr, UINTN Size)
{
    UINTN RangeStart = BaseAddr & PHYS_PAGE_ADDR_MASK;
    UINTN Pages      = (Size + PAGE_SIZE - 1) / PAGE_SIZE;
    UINTN RangeEnd   = RangeStart + (Pages * PAGE_SIZE);

    for (UINTN Addr = RangeStart; Addr < RangeEnd; Addr += PAGE_SIZE)
    {
        IdentityMapPage(Addr);
    }

    return RangeEnd;
}

/**
 * Function: MemoryManager::MapRange
 * Description: Maps a contiguous range of pages from a physical base to a virtual base.
 * Parameters:
 *   UINTN PhysicalAddr - Physical base address for the mapping.
 *   UINTN VirtualAddr - Virtual base address for the mapping.
 *   UINTN Pages - Number of pages to map.
 * Returns:
 *   UINTN - Virtual end address (exclusive) after mapping.
 */
UINTN MemoryManager::MapRange(UINTN PhysicalAddr, UINTN VirtualAddr, UINTN Pages)
{
    UINTN RangeStartPhysical = PhysicalAddr & PHYS_PAGE_ADDR_MASK;
    UINTN RangeStartVirtual  = VirtualAddr & PHYS_PAGE_ADDR_MASK;

    for (UINTN i = 0; i < Pages; i++)
    {
        UINTN Offset = i * PAGE_SIZE;
        MapPage(RangeStartPhysical + Offset, RangeStartVirtual + Offset);
    }

    return RangeStartVirtual + (Pages * PAGE_SIZE);
}

/**
 * Function: MemoryManager::MapKernelToHigherHalf
 * Description: Maps the kernel image into the higher-half virtual address region.
 * Parameters:
 *   UINTN PhysicalAddr - Physical base address of the kernel image.
 *   UINTN Size - Kernel image size in bytes.
 * Returns:
 *   UINTN - Virtual end address (exclusive) of the mapped kernel range.
 */
UINTN MemoryManager::MapKernelToHigherHalf(UINTN PhysicalAddr, UINTN Size)
{
    UINTN Pages = (Size + PAGE_SIZE - 1) / PAGE_SIZE;

    return MapRange(PhysicalAddr, KERNEL_BASE_VIRTUAL_ADDR, Pages);
}

/**
 * Function: MemoryManager::IdentityMapMemoryMap
 * Description: Identity-maps all pages described in the current UEFI memory map.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
void MemoryManager::IdentityMapMemoryMap()
{
    for (UINTN i = 0; i < MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize; i++)
    {
        EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        for (UINTN j = 0; j < desc->NumberOfPages; j++)
        {
            IdentityMapPage(desc->PhysicalStart + (j * PAGE_SIZE));
        }
    }
}

/**
 * Function: MemoryManager::InitPaging
 * Description: Activates paging by loading the current PML4 table address into CR3.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
void MemoryManager::InitPaging()
{
    UINT64 pml4_phys = (UINT64) PageMapL4Table;

    asm volatile("mov %0, %%cr3" ::"r"(pml4_phys) : "memory");
}
