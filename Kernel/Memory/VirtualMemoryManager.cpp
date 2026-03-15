#include <CommonUtils.hpp>
#include <Memory/VirtualMemoryManager.hpp>

VirtualMemoryManager::VirtualMemoryManager(UINTN PageMapL4TableAddr, PhysicalMemoryManager& PMM) : PageMapL4Table((PageTableEntry*) PageMapL4TableAddr), PMM(PMM)
{
}

bool VirtualMemoryManager::MapPage(UINTN PhysicalAddr, UINTN VirtualAddr)
{
    VirtualAddress Vaddr;
    Vaddr.value = VirtualAddr;

    UINTN PageMapL4TableIndex            = Vaddr.fields.pml4_index;
    UINTN PageDirectoryPointerTableIndex = Vaddr.fields.pdpt_index;
    UINTN PageDirectoryTableIndex        = Vaddr.fields.pd_index;
    UINTN PageTableIndex                 = Vaddr.fields.pt_index;

    PageTableEntry PmL4Entry = PageMapL4Table[PageMapL4TableIndex];

    if (!PmL4Entry.fields.present)
    {
        void* PageDirectoryPointerTableAddr = PMM.AllocatePagesFromMemoryMap(1);
        if (PageDirectoryPointerTableAddr == NULL)
            return false;

        PageTableEntry PageDirectoryPointerTable = {0};
        PageDirectoryPointerTable.value          = (UINTN) PageDirectoryPointerTableAddr;

        kmemset(PageDirectoryPointerTableAddr, 0, PAGE_SIZE);

        PageDirectoryPointerTable.fields.present     = 1;
        PageDirectoryPointerTable.fields.writeable   = 1;
        PageDirectoryPointerTable.fields.user_access = 0;

        PageMapL4Table[PageMapL4TableIndex] = PageDirectoryPointerTable;
        PmL4Entry                           = PageMapL4Table[PageMapL4TableIndex];
    }

    PageTableEntry* PageDirectoryPointerTable = (PageTableEntry*) (PmL4Entry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PDPTEntry                 = PageDirectoryPointerTable[PageDirectoryPointerTableIndex];

    if (!PDPTEntry.fields.present)
    {
        void* PageDirectoryTableAddr = PMM.AllocatePagesFromMemoryMap(1);
        if (PageDirectoryTableAddr == NULL)
            return false;

        PageTableEntry PageDirectoryTable = {0};
        PageDirectoryTable.value          = (UINTN) PageDirectoryTableAddr;

        kmemset(PageDirectoryTableAddr, 0, PAGE_SIZE);

        PageDirectoryTable.fields.present     = 1;
        PageDirectoryTable.fields.writeable   = 1;
        PageDirectoryTable.fields.user_access = 0;

        PageDirectoryPointerTable[PageDirectoryPointerTableIndex] = PageDirectoryTable;
        PDPTEntry                                                 = PageDirectoryPointerTable[PageDirectoryPointerTableIndex];
    }

    PageTableEntry* PageDirectoryTable = (PageTableEntry*) (PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PDTEntry           = PageDirectoryTable[PageDirectoryTableIndex];

    if (!PDTEntry.fields.present)
    {
        void* PageTableAddr = PMM.AllocatePagesFromMemoryMap(1);
        if (PageTableAddr == NULL)
            return false;

        PageTableEntry PageTable = {0};
        PageTable.value          = (UINTN) PageTableAddr;

        kmemset(PageTableAddr, 0, PAGE_SIZE);

        PageTable.fields.present     = 1;
        PageTable.fields.writeable   = 1;
        PageTable.fields.user_access = 0;

        PageDirectoryTable[PageDirectoryTableIndex] = PageTable;
        PDTEntry                                    = PageDirectoryTable[PageDirectoryTableIndex];
    }

    PageTableEntry* PageTable = (PageTableEntry*) (PDTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PTEntry   = PageTable[PageTableIndex];

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

UINTN VirtualMemoryManager::MapRange(UINTN PhysicalAddr, UINTN VirtualAddr, UINTN Pages)
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

bool VirtualMemoryManager::UnmapPage(UINTN VirtualAddr)
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

    __asm__("invlpg (%0)\n" : : "r"(VirtualAddr));

    return true;
}

UINTN VirtualMemoryManager::UnmapRange(UINTN VirtualAddr, UINTN Pages)
{
    UINTN RangeStartVirtual = VirtualAddr & PHYS_PAGE_ADDR_MASK;

    for (UINTN i = 0; i < Pages; i++)
    {
        UINTN Offset = i * PAGE_SIZE;
        UnmapPage(RangeStartVirtual + Offset);
    }

    return RangeStartVirtual + (Pages * PAGE_SIZE);
}
