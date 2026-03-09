#include <MemoryManager.hpp>

void kmemset(void* dest, int value, size_t count)
{
    uint8_t* ptr = (uint8_t*)dest;
    uint8_t byte = (uint8_t)value;

    for (size_t i = 0; i < count; i++)
        ptr[i] = byte;

}   

MemoryManager::MemoryManager(MemoryMapInfo MemoryMap, Console* efiConsole)
    : MemoryMap(MemoryMap), efiConsole(efiConsole)
{
    PageMapL4Table = (PageTableEntry*) AllocateAvailablePagesFromMemoryMap(1);
    kmemset((void*) PageMapL4Table, 0, PAGE_SIZE * 1);

}

MemoryManager::~MemoryManager()
{
}

void* MemoryManager::AllocateAvailablePagesFromMemoryMap(UINTN Pages)
{
    static void* NextPageAddress            = NULL;
    static UINTN CurrentDescriptor          = 0;
    static UINTN RemainingPagesInDescriptor = 0;

    if (RemainingPagesInDescriptor < Pages)
    {
        for (UINTN i = CurrentDescriptor + 1; i < MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize; i++)
        {
            EFI_MEMORY_DESCRIPTOR* desc
                    = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

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

bool MemoryManager::MapPage(UINTN PhysicalAddr, UINTN VirtualAddr)
{

    VirtualAddress Vaddr;
    Vaddr.value = VirtualAddr;

    UINTN PageMapL4TableIndex = Vaddr.fields.pml4_index;
    UINTN PageDirectoryPointerTableIndex = Vaddr.fields.pdpt_index;
    UINTN PageDirectoryTableIndex = Vaddr.fields.pd_index;
    UINTN PageTableIndex = Vaddr.fields.pt_index;

    PageTableEntry PmL4Entry = PageMapL4Table[PageMapL4TableIndex];

    // If No PageDirectoryPointerTable
    if (!PmL4Entry.fields.present){

        void* PageDirectoryPointerTableAddr = AllocateAvailablePagesFromMemoryMap(1);
        PageTableEntry PageDirectoryPointerTable = {0};
        PageDirectoryPointerTable.value = (UINTN) PageDirectoryPointerTableAddr;

        kmemset(PageDirectoryPointerTableAddr, 0, PAGE_SIZE);

        PageDirectoryPointerTable.fields.present = 1;
        PageDirectoryPointerTable.fields.writeable = 1;
        PageDirectoryPointerTable.fields.user_access = 1;

        PageMapL4Table[PageMapL4TableIndex] = PageDirectoryPointerTable;
    }

    PageTableEntry* PageDirectoryPointerTable = (PageTableEntry*) (PmL4Entry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry PDPTEntry = PageDirectoryPointerTable[PageDirectoryPointerTableIndex];

    // If No PageDirectoryTable
    if (!PDPTEntry.fields.present){
        void* PageDirectoryTableAddr = AllocateAvailablePagesFromMemoryMap(1);
        PageTableEntry PageDirectoryTable = {0};
        PageDirectoryTable.value = (UINTN) PageDirectoryTableAddr;

        kmemset(PageDirectoryTableAddr, 0, PAGE_SIZE);

        PageDirectoryTable.fields.present = 1;
        PageDirectoryTable.fields.writeable = 1;
        PageDirectoryTable.fields.user_access = 1;

        PageDirectoryPointerTable[PageDirectoryPointerTableIndex] = PageDirectoryTable;
    }

    PageTableEntry* PageDirectoryTable = (PageTableEntry*) (PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry PDTEntry = PageDirectoryTable[PageDirectoryTableIndex];

    // If No PageTable
    if (!PDTEntry.fields.present){
        void* PageTableAddr = AllocateAvailablePagesFromMemoryMap(1);
        PageTableEntry PageTable = {0};
        PageTable.value = (UINTN) PageTableAddr;

        kmemset(PageTableAddr, 0, PAGE_SIZE);

        PageTable.fields.present = 1;
        PageTable.fields.writeable = 1;
        PageTable.fields.user_access = 1;

        PageDirectoryTable[PageDirectoryTableIndex] = PageTable;
    }

    PageTableEntry* PageTable = (PageTableEntry*) (PDTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry PTEntry = PageTable[PageTableIndex];

    // If no Entry in page table
    if (!PTEntry.fields.present){
        PageTableEntry NewPTEntry = {0};
        NewPTEntry.value = PhysicalAddr & PHYS_PAGE_ADDR_MASK;

        NewPTEntry.fields.present = 1;
        NewPTEntry.fields.writeable = 1;
        NewPTEntry.fields.user_access = 1;

        PageTable[PageTableIndex] = NewPTEntry;
    }

    return true;

}

bool MemoryManager::UnmapPage(UINTN VirtualAddr){
    VirtualAddress Vaddr;
    Vaddr.value = VirtualAddr;

    UINTN PageMapL4TableIndex = Vaddr.fields.pml4_index;
    UINTN PageDirectoryPointerTableIndex = Vaddr.fields.pdpt_index;
    UINTN PageDirectoryTableIndex = Vaddr.fields.pd_index;
    UINTN PageTableIndex = Vaddr.fields.pt_index;

    PageTableEntry PmL4Entry = PageMapL4Table[PageMapL4TableIndex];

    PageTableEntry* PageDirectoryPointerTable = (PageTableEntry*) (PmL4Entry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry PDPTEntry = PageDirectoryPointerTable[PageDirectoryPointerTableIndex];

    PageTableEntry* PageDirectoryTable = (PageTableEntry*) (PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry PDTEntry = PageDirectoryTable[PageDirectoryTableIndex];

    PageTableEntry* PageTable = (PageTableEntry*) (PDTEntry.value & PHYS_PAGE_ADDR_MASK);
    
    PageTable[PageTableIndex].value = 0;

    // Flush the TLB cache for this page
    __asm__ ("invlpg (%0)\n" : : "r"(VirtualAddr));

    return true;
}