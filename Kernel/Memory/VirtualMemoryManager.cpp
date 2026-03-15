#include <CommonUtils.hpp>
#include <Memory/VirtualMemoryManager.hpp>

VirtualMemoryManager::VirtualMemoryManager(UINTN PageMapL4TableAddr, PhysicalMemoryManager& PMM) : PageMapL4Table((PageTableEntry*) PageMapL4TableAddr), PMM(PMM)
{
}

bool VirtualMemoryManager::MapPage(UINTN PhysicalAddr, UINTN VirtualAddr, bool UserAccess)
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
        PageDirectoryPointerTable.fields.user_access = UserAccess;

        PageMapL4Table[PageMapL4TableIndex] = PageDirectoryPointerTable;
        PmL4Entry                           = PageMapL4Table[PageMapL4TableIndex];
    }
    else if (UserAccess && !PmL4Entry.fields.user_access)
    {
        PmL4Entry.fields.user_access        = 1;
        PageMapL4Table[PageMapL4TableIndex] = PmL4Entry;
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
        PageDirectoryTable.fields.user_access = UserAccess;

        PageDirectoryPointerTable[PageDirectoryPointerTableIndex] = PageDirectoryTable;
        PDPTEntry                                                 = PageDirectoryPointerTable[PageDirectoryPointerTableIndex];
    }
    else if (UserAccess && !PDPTEntry.fields.user_access)
    {
        PDPTEntry.fields.user_access                              = 1;
        PageDirectoryPointerTable[PageDirectoryPointerTableIndex] = PDPTEntry;
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
        PageTable.fields.user_access = UserAccess;

        PageDirectoryTable[PageDirectoryTableIndex] = PageTable;
        PDTEntry                                    = PageDirectoryTable[PageDirectoryTableIndex];
    }
    else if (UserAccess && !PDTEntry.fields.user_access)
    {
        PDTEntry.fields.user_access                 = 1;
        PageDirectoryTable[PageDirectoryTableIndex] = PDTEntry;
    }

    PageTableEntry* PageTable  = (PageTableEntry*) (PDTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  NewPTEntry = {0};
    NewPTEntry.value           = PhysicalAddr & PHYS_PAGE_ADDR_MASK;

    NewPTEntry.fields.present     = 1;
    NewPTEntry.fields.writeable   = 1;
    NewPTEntry.fields.user_access = UserAccess;

    PageTable[PageTableIndex] = NewPTEntry;

    return true;
}

UINTN VirtualMemoryManager::MapRange(UINTN PhysicalAddr, UINTN VirtualAddr, UINTN Pages, bool UserAccess)
{
    UINTN RangeStartPhysical = PhysicalAddr & PHYS_PAGE_ADDR_MASK;
    UINTN RangeStartVirtual  = VirtualAddr & PHYS_PAGE_ADDR_MASK;

    for (UINTN i = 0; i < Pages; i++)
    {
        UINTN Offset = i * PAGE_SIZE;
        MapPage(RangeStartPhysical + Offset, RangeStartVirtual + Offset, UserAccess);
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

PageTableEntry* VirtualMemoryManager::CopyPageMapL4Table()
{
    void* NewPML4Addr = PMM.AllocatePagesFromMemoryMap(1);
    if (NewPML4Addr == NULL)
    {
        return NULL;
    }

    PageTableEntry* NewPML4 = (PageTableEntry*) NewPML4Addr;
    kmemset(NewPML4, 0, PAGE_SIZE);

    for (UINTN PML4Index = 0; PML4Index < 512; PML4Index++)
    {
        PageTableEntry PML4Entry = PageMapL4Table[PML4Index];
        if (!PML4Entry.fields.present)
        {
            continue;
        }

        void* NewPDPTAddr = PMM.AllocatePagesFromMemoryMap(1);
        if (NewPDPTAddr == NULL)
        {
            return NULL;
        }

        kmemset(NewPDPTAddr, 0, PAGE_SIZE);

        PageTableEntry* OldPDPT = (PageTableEntry*) (PML4Entry.value & PHYS_PAGE_ADDR_MASK);
        PageTableEntry* NewPDPT = (PageTableEntry*) NewPDPTAddr;

        for (UINTN PDPTIndex = 0; PDPTIndex < 512; PDPTIndex++)
        {
            PageTableEntry PDPTEntry = OldPDPT[PDPTIndex];
            if (!PDPTEntry.fields.present)
            {
                continue;
            }

            void* NewPDAddr = PMM.AllocatePagesFromMemoryMap(1);
            if (NewPDAddr == NULL)
            {
                return NULL;
            }

            kmemset(NewPDAddr, 0, PAGE_SIZE);

            PageTableEntry* OldPD = (PageTableEntry*) (PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
            PageTableEntry* NewPD = (PageTableEntry*) NewPDAddr;

            for (UINTN PDIndex = 0; PDIndex < 512; PDIndex++)
            {
                PageTableEntry PDEntry = OldPD[PDIndex];
                if (!PDEntry.fields.present)
                {
                    continue;
                }

                void* NewPTAddr = PMM.AllocatePagesFromMemoryMap(1);
                if (NewPTAddr == NULL)
                {
                    return NULL;
                }

                kmemset(NewPTAddr, 0, PAGE_SIZE);

                PageTableEntry* OldPT = (PageTableEntry*) (PDEntry.value & PHYS_PAGE_ADDR_MASK);
                PageTableEntry* NewPT = (PageTableEntry*) NewPTAddr;

                for (UINTN PTIndex = 0; PTIndex < 512; PTIndex++)
                {
                    NewPT[PTIndex] = OldPT[PTIndex];
                }

                PageTableEntry NewPDEntry = PDEntry;
                NewPDEntry.value          = ((UINTN) NewPTAddr & PHYS_PAGE_ADDR_MASK) | (PDEntry.value & ~PHYS_PAGE_ADDR_MASK);
                NewPD[PDIndex]            = NewPDEntry;
            }

            PageTableEntry NewPDPTEntry = PDPTEntry;
            NewPDPTEntry.value          = ((UINTN) NewPDAddr & PHYS_PAGE_ADDR_MASK) | (PDPTEntry.value & ~PHYS_PAGE_ADDR_MASK);
            NewPDPT[PDPTIndex]          = NewPDPTEntry;
        }

        PageTableEntry NewPML4Entry = PML4Entry;
        NewPML4Entry.value          = ((UINTN) NewPDPTAddr & PHYS_PAGE_ADDR_MASK) | (PML4Entry.value & ~PHYS_PAGE_ADDR_MASK);
        NewPML4[PML4Index]          = NewPML4Entry;
    }

    return NewPML4;
}
