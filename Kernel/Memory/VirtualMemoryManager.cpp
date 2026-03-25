/**
 * File: VirtualMemoryManager.cpp
 * Author: Marwan Mostafa
 * Description: Virtual memory paging management implementation.
 */

#include <CommonUtils.hpp>
#include <Memory/VirtualMemoryManager.hpp>

namespace
{
void FreeClonedUserPagingHierarchy(PageTableEntry* NewPML4, PhysicalMemoryManager& PMM)
{
    if (NewPML4 == NULL)
    {
        return;
    }

    for (UINTN PML4Index = 0; PML4Index < 256; PML4Index++)
    {
        PageTableEntry PML4Entry = NewPML4[PML4Index];
        if (!PML4Entry.fields.present)
        {
            continue;
        }

        PageTableEntry* NewPDPT = (PageTableEntry*) (PML4Entry.value & PHYS_PAGE_ADDR_MASK);
        if (NewPDPT == NULL)
        {
            continue;
        }

        for (UINTN PDPTIndex = 0; PDPTIndex < 512; PDPTIndex++)
        {
            PageTableEntry PDPTEntry = NewPDPT[PDPTIndex];
            if (!PDPTEntry.fields.present || PDPTEntry.fields.size)
            {
                continue;
            }

            PageTableEntry* NewPD = (PageTableEntry*) (PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
            if (NewPD == NULL)
            {
                continue;
            }

            for (UINTN PDIndex = 0; PDIndex < 512; PDIndex++)
            {
                PageTableEntry PDEntry = NewPD[PDIndex];
                if (!PDEntry.fields.present || PDEntry.fields.size)
                {
                    continue;
                }

                PageTableEntry* NewPT = (PageTableEntry*) (PDEntry.value & PHYS_PAGE_ADDR_MASK);
                if (NewPT != NULL)
                {
                    PMM.FreePagesFromDescriptor(NewPT, 1);
                }
            }

            PMM.FreePagesFromDescriptor(NewPD, 1);
        }

        PMM.FreePagesFromDescriptor(NewPDPT, 1);
    }

    PMM.FreePagesFromDescriptor(NewPML4, 1);
}
}

/**
 * Function: VirtualMemoryManager::VirtualMemoryManager
 * Description: Initializes the virtual memory manager with the active PML4 table and physical memory manager.
 * Parameters:
 *   UINTN PageMapL4TableAddr - Physical address of the level-4 page table.
 *   PhysicalMemoryManager& PMM - Reference to the physical memory manager used for page table allocations.
 * Returns:
 *   VirtualMemoryManager - Constructed virtual memory manager instance.
 */
VirtualMemoryManager::VirtualMemoryManager(UINTN PageMapL4TableAddr, PhysicalMemoryManager& PMM) : PageMapL4Table((PageTableEntry*) PageMapL4TableAddr), PMM(PMM),
                                                                                                        LastCopyPageMapL4DebugInfo{COPY_PML4_FAIL_NONE, 0xFFFF, 0xFFFF, 0xFFFF, 0, 0, 0, 0},
                                                                                                        LastPageTableMutationDebugInfo{PAGE_TABLE_MUTATION_NONE, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0, 0}
{
}

/**
 * Function: VirtualMemoryManager::MapPage
 * Description: Maps one physical page to one virtual page and creates intermediate page tables when required.
 * Parameters:
 *   UINTN PhysicalAddr - Physical address to map.
 *   UINTN VirtualAddr - Virtual address to map to.
 *   const PageMappingFlags& Flags - Access and write flags to apply to the mapping.
 * Returns:
 *   bool - True if the page was mapped, false if allocation of a paging structure failed.
 */
bool VirtualMemoryManager::MapPage(UINTN PhysicalAddr, UINTN VirtualAddr, const PageMappingFlags& Flags)
{
    VirtualAddress Vaddr;
    Vaddr.value = VirtualAddr;

    UINTN PageMapL4TableIndex            = Vaddr.fields.pml4_index;
    UINTN PageDirectoryPointerTableIndex = Vaddr.fields.pdpt_index;
    UINTN PageDirectoryTableIndex        = Vaddr.fields.pd_index;
    UINTN PageTableIndex                 = Vaddr.fields.pt_index;

    const bool TrackSuspiciousSlot = (PageMapL4TableIndex == 0 && PageDirectoryPointerTableIndex == 2 && PageDirectoryTableIndex == 0);

    auto RecordMutation = [&](PageTableMutationEvent Event, uint64_t EntryValue, uint64_t DerivedAddress) {
        if (!TrackSuspiciousSlot)
        {
            return;
        }

        LastPageTableMutationDebugInfo.Event          = Event;
        LastPageTableMutationDebugInfo.PML4Index      = static_cast<uint16_t>(PageMapL4TableIndex);
        LastPageTableMutationDebugInfo.PDPTIndex      = static_cast<uint16_t>(PageDirectoryPointerTableIndex);
        LastPageTableMutationDebugInfo.PDIndex        = static_cast<uint16_t>(PageDirectoryTableIndex);
        LastPageTableMutationDebugInfo.PTIndex        = static_cast<uint16_t>(PageTableIndex);
        LastPageTableMutationDebugInfo.EntryValue     = EntryValue;
        LastPageTableMutationDebugInfo.DerivedAddress = DerivedAddress;
    };

    PageTableEntry PmL4Entry = PageMapL4Table[PageMapL4TableIndex];

    if (!PmL4Entry.fields.present)
    {
        void* PageDirectoryPointerTableAddr = PMM.AllocatePagesFromDescriptor(1);
        if (PageDirectoryPointerTableAddr == NULL)
            return false;

        PageTableEntry PageDirectoryPointerTable = {0};
        PageDirectoryPointerTable.value          = (UINTN) PageDirectoryPointerTableAddr;

        kmemset(PageDirectoryPointerTableAddr, 0, PAGE_SIZE);

        PageDirectoryPointerTable.fields.present     = 1;
        PageDirectoryPointerTable.fields.writeable   = 1;
        PageDirectoryPointerTable.fields.user_access = Flags.UserAccess;

        PageMapL4Table[PageMapL4TableIndex] = PageDirectoryPointerTable;
        RecordMutation(PAGE_TABLE_MUTATION_WRITE_PML4, PageDirectoryPointerTable.value, PageDirectoryPointerTable.value & PHYS_PAGE_ADDR_MASK);
        PmL4Entry                           = PageMapL4Table[PageMapL4TableIndex];
    }
    else if (Flags.UserAccess && !PmL4Entry.fields.user_access)
    {
        PmL4Entry.fields.user_access        = 1;
        PageMapL4Table[PageMapL4TableIndex] = PmL4Entry;
    }

    PageTableEntry* PageDirectoryPointerTable = (PageTableEntry*) (PmL4Entry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PDPTEntry                 = PageDirectoryPointerTable[PageDirectoryPointerTableIndex];

    if (!PDPTEntry.fields.present)
    {
        void* PageDirectoryTableAddr = PMM.AllocatePagesFromDescriptor(1);
        if (PageDirectoryTableAddr == NULL)
            return false;

        PageTableEntry PageDirectoryTable = {0};
        PageDirectoryTable.value          = (UINTN) PageDirectoryTableAddr;

        kmemset(PageDirectoryTableAddr, 0, PAGE_SIZE);

        PageDirectoryTable.fields.present     = 1;
        PageDirectoryTable.fields.writeable   = 1;
        PageDirectoryTable.fields.user_access = Flags.UserAccess;

        PageDirectoryPointerTable[PageDirectoryPointerTableIndex] = PageDirectoryTable;
        RecordMutation(PAGE_TABLE_MUTATION_WRITE_PDPT, PageDirectoryTable.value, PageDirectoryTable.value & PHYS_PAGE_ADDR_MASK);
        PDPTEntry                                                 = PageDirectoryPointerTable[PageDirectoryPointerTableIndex];
    }
    else if (Flags.UserAccess && !PDPTEntry.fields.user_access)
    {
        PDPTEntry.fields.user_access                              = 1;
        PageDirectoryPointerTable[PageDirectoryPointerTableIndex] = PDPTEntry;
    }

    PageTableEntry* PageDirectoryTable = (PageTableEntry*) (PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PDTEntry           = PageDirectoryTable[PageDirectoryTableIndex];

    if (!PDTEntry.fields.present)
    {
        void* PageTableAddr = PMM.AllocatePagesFromDescriptor(1);
        if (PageTableAddr == NULL)
            return false;

        PageTableEntry PageTable = {0};
        PageTable.value          = (UINTN) PageTableAddr;

        kmemset(PageTableAddr, 0, PAGE_SIZE);

        PageTable.fields.present     = 1;
        PageTable.fields.writeable   = 1;
        PageTable.fields.user_access = Flags.UserAccess;

        PageDirectoryTable[PageDirectoryTableIndex] = PageTable;
        RecordMutation(PAGE_TABLE_MUTATION_WRITE_PD, PageTable.value, PageTable.value & PHYS_PAGE_ADDR_MASK);
        PDTEntry                                    = PageDirectoryTable[PageDirectoryTableIndex];
    }
    else if (Flags.UserAccess && !PDTEntry.fields.user_access)
    {
        PDTEntry.fields.user_access                 = 1;
        PageDirectoryTable[PageDirectoryTableIndex] = PDTEntry;
    }

    if (TrackSuspiciousSlot && !PDTEntry.fields.size)
    {
        uint64_t ExistingPTAddr = static_cast<uint64_t>(PDTEntry.value & PHYS_PAGE_ADDR_MASK);
        if ((ExistingPTAddr & 0xFFFF000000000000ULL) != 0)
        {
            RecordMutation(PAGE_TABLE_MUTATION_OBSERVE_PD_NONCANONICAL, PDTEntry.value, ExistingPTAddr);
        }
    }

    PageTableEntry* PageTable  = (PageTableEntry*) (PDTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  NewPTEntry = {0};
    NewPTEntry.value           = PhysicalAddr & PHYS_PAGE_ADDR_MASK;

    NewPTEntry.fields.present     = 1;
    NewPTEntry.fields.writeable   = Flags.Writeable;
    NewPTEntry.fields.user_access = Flags.UserAccess;

    PageTable[PageTableIndex] = NewPTEntry;
    RecordMutation(PAGE_TABLE_MUTATION_WRITE_PT, NewPTEntry.value, NewPTEntry.value & PHYS_PAGE_ADDR_MASK);

    return true;
}

bool VirtualMemoryManager::ProtectPage(UINTN VirtualAddr, bool UserAccess, bool Writeable, bool Executable)
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
        return false;
    }

    PageTableEntry* PageDirectoryPointerTable = (PageTableEntry*) (PmL4Entry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PDPTEntry                 = PageDirectoryPointerTable[PageDirectoryPointerTableIndex];
    if (!PDPTEntry.fields.present)
    {
        return false;
    }

    if (PDPTEntry.fields.size)
    {
        return false;
    }

    PageTableEntry* PageDirectoryTable = (PageTableEntry*) (PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PDTEntry           = PageDirectoryTable[PageDirectoryTableIndex];
    if (!PDTEntry.fields.present)
    {
        return false;
    }

    if (PDTEntry.fields.size)
    {
        return false;
    }

    PageTableEntry* PageTable = (PageTableEntry*) (PDTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PTEntry   = PageTable[PageTableIndex];
    if (!PTEntry.fields.present)
    {
        return false;
    }

    PTEntry.fields.user_access        = UserAccess ? 1U : 0U;
    PTEntry.fields.writeable          = Writeable ? 1U : 0U;
    PTEntry.fields.execution_disabled = Executable ? 0U : 1U;
    PageTable[PageTableIndex]         = PTEntry;

    __asm__("invlpg (%0)\n" : : "r"(VirtualAddr) : "memory");
    return true;
}

/**
 * Function: VirtualMemoryManager::MapRange
 * Description: Maps a contiguous range of pages from a physical range to a virtual range.
 * Parameters:
 *   UINTN PhysicalAddr - Starting physical address.
 *   UINTN VirtualAddr - Starting virtual address.
 *   UINTN Pages - Number of pages to map.
 *   const PageMappingFlags& Flags - Access and write flags for each page mapping.
 * Returns:
 *   UINTN - First virtual address immediately after the mapped range.
 */
UINTN VirtualMemoryManager::MapRange(UINTN PhysicalAddr, UINTN VirtualAddr, UINTN Pages, const PageMappingFlags& Flags)
{
    UINTN RangeStartPhysical = PhysicalAddr & PHYS_PAGE_ADDR_MASK;
    UINTN RangeStartVirtual  = VirtualAddr & PHYS_PAGE_ADDR_MASK;
    UINTN CurrentPhysical    = RangeStartPhysical;
    UINTN CurrentVirtual     = RangeStartVirtual;

    for (UINTN i = 0; i < Pages; i++)
    {
        MapPage(CurrentPhysical, CurrentVirtual, Flags);

        if (CurrentPhysical > (UINT64_MAX - PAGE_SIZE) || CurrentVirtual > (UINT64_MAX - PAGE_SIZE))
        {
            return CurrentVirtual;
        }

        CurrentPhysical += PAGE_SIZE;
        CurrentVirtual += PAGE_SIZE;
    }

    return CurrentVirtual;
}

UINTN VirtualMemoryManager::ProtectRange(UINTN VirtualAddr, UINTN Pages, bool UserAccess, bool Writeable, bool Executable)
{
    UINTN RangeStartVirtual = VirtualAddr & PHYS_PAGE_ADDR_MASK;
    UINTN CurrentVirtual    = RangeStartVirtual;

    for (UINTN i = 0; i < Pages; i++)
    {
        ProtectPage(CurrentVirtual, UserAccess, Writeable, Executable);

        if (CurrentVirtual > (UINT64_MAX - PAGE_SIZE))
        {
            return CurrentVirtual;
        }

        CurrentVirtual += PAGE_SIZE;
    }

    return CurrentVirtual;
}

/**
 * Function: VirtualMemoryManager::UnmapPage
 * Description: Removes the mapping for a single virtual page and invalidates the TLB entry.
 * Parameters:
 *   UINTN VirtualAddr - Virtual address of the page to unmap.
 * Returns:
 *   bool - True if a valid mapping path existed and was cleared, false otherwise.
 */
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

    if (PDPTEntry.fields.size)
        return false;

    PageTableEntry* PageDirectoryTable = (PageTableEntry*) (PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
    PageTableEntry  PDTEntry           = PageDirectoryTable[PageDirectoryTableIndex];

    if (!PDTEntry.fields.present)
        return false;

    if (PDTEntry.fields.size)
        return false;

    PageTableEntry* PageTable = (PageTableEntry*) (PDTEntry.value & PHYS_PAGE_ADDR_MASK);

    PageTable[PageTableIndex].value = 0;

    __asm__("invlpg (%0)\n" : : "r"(VirtualAddr));

    return true;
}

/**
 * Function: VirtualMemoryManager::UnmapRange
 * Description: Removes mappings for a contiguous range of virtual pages.
 * Parameters:
 *   UINTN VirtualAddr - Starting virtual address of the range.
 *   UINTN Pages - Number of pages to unmap.
 * Returns:
 *   UINTN - First virtual address immediately after the unmapped range.
 */
UINTN VirtualMemoryManager::UnmapRange(UINTN VirtualAddr, UINTN Pages)
{
    UINTN RangeStartVirtual = VirtualAddr & PHYS_PAGE_ADDR_MASK;
    UINTN CurrentVirtual    = RangeStartVirtual;

    for (UINTN i = 0; i < Pages; i++)
    {
        UnmapPage(CurrentVirtual);

        if (CurrentVirtual > (UINT64_MAX - PAGE_SIZE))
        {
            return CurrentVirtual;
        }

        CurrentVirtual += PAGE_SIZE;
    }

    return CurrentVirtual;
}

/**
 * Function: VirtualMemoryManager::CopyPageMapL4Table
 * Description: Deep-copies the current paging hierarchy and returns a new PML4 root table.
 * Parameters:
 *   None
 * Returns:
 *   PageTableEntry* - Pointer to the copied PML4 table, or NULL on allocation failure.
 */
PageTableEntry* VirtualMemoryManager::CopyPageMapL4Table()
{
    LastCopyPageMapL4DebugInfo = {COPY_PML4_FAIL_NONE, 0xFFFF, 0xFFFF, 0xFFFF, 0, 0, 0, 0};

    uint64_t AllocationAttempts = 0;

    auto RecordFailure = [&](CopyPageMapL4FailureStage FailureStage, uint16_t PML4Index, uint16_t PDPTIndex, uint16_t PDIndex, uint64_t SourceEntryValue,
                             uint64_t DerivedAddress) {
        LastCopyPageMapL4DebugInfo.FailureStage      = FailureStage;
        LastCopyPageMapL4DebugInfo.PML4Index         = PML4Index;
        LastCopyPageMapL4DebugInfo.PDPTIndex         = PDPTIndex;
        LastCopyPageMapL4DebugInfo.PDIndex           = PDIndex;
        LastCopyPageMapL4DebugInfo.SourceEntryValue  = SourceEntryValue;
        LastCopyPageMapL4DebugInfo.DerivedAddress    = DerivedAddress;
        LastCopyPageMapL4DebugInfo.AllocationAttempts = AllocationAttempts;
    };

    ++AllocationAttempts;
    void* NewPML4Addr = PMM.AllocatePagesFromDescriptor(1);
    if (NewPML4Addr == NULL)
    {
        RecordFailure(COPY_PML4_FAIL_ALLOC_PML4, 0xFFFF, 0xFFFF, 0xFFFF, 0, 0);
        return NULL;
    }

    PageTableEntry* NewPML4 = (PageTableEntry*) NewPML4Addr;
    kmemset(NewPML4, 0, PAGE_SIZE);

    auto FailCopy = [&]() -> PageTableEntry* {
        FreeClonedUserPagingHierarchy(NewPML4, PMM);
        return NULL;
    };

    for (UINTN PML4Index = 0; PML4Index < 512; PML4Index++)
    {
        PageTableEntry PML4Entry = PageMapL4Table[PML4Index];
        if (!PML4Entry.fields.present)
        {
            continue;
        }

        if (PML4Index >= 256)
        {
            NewPML4[PML4Index] = PML4Entry;
            continue;
        }

        ++AllocationAttempts;
        void* NewPDPTAddr = PMM.AllocatePagesFromDescriptor(1);
        if (NewPDPTAddr == NULL)
        {
            RecordFailure(COPY_PML4_FAIL_ALLOC_PDPT, static_cast<uint16_t>(PML4Index), 0xFFFF, 0xFFFF, PML4Entry.value, 0);
            return FailCopy();
        }

        kmemset(NewPDPTAddr, 0, PAGE_SIZE);

        UINTN OldPDPTAddr       = (UINTN) (PML4Entry.value & PHYS_PAGE_ADDR_MASK);
        if ((OldPDPTAddr & 0xFFFF000000000000ULL) != 0)
        {
            RecordFailure(COPY_PML4_FAIL_INVALID_OLD_PDPT, static_cast<uint16_t>(PML4Index), 0xFFFF, 0xFFFF, PML4Entry.value, OldPDPTAddr);
            return FailCopy();
        }

        PageTableEntry* OldPDPT = (PageTableEntry*) OldPDPTAddr;
        PageTableEntry* NewPDPT = (PageTableEntry*) NewPDPTAddr;

        for (UINTN PDPTIndex = 0; PDPTIndex < 512; PDPTIndex++)
        {
            PageTableEntry PDPTEntry = OldPDPT[PDPTIndex];
            if (!PDPTEntry.fields.present)
            {
                continue;
            }

            if (PDPTEntry.fields.size)
            {
                NewPDPT[PDPTIndex] = PDPTEntry;
                continue;
            }

            ++AllocationAttempts;
            void* NewPDAddr = PMM.AllocatePagesFromDescriptor(1);
            if (NewPDAddr == NULL)
            {
                RecordFailure(COPY_PML4_FAIL_ALLOC_PD, static_cast<uint16_t>(PML4Index), static_cast<uint16_t>(PDPTIndex), 0xFFFF, PDPTEntry.value, 0);
                return FailCopy();
            }

            kmemset(NewPDAddr, 0, PAGE_SIZE);

            UINTN OldPDAddr       = (UINTN) (PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
            if ((OldPDAddr & 0xFFFF000000000000ULL) != 0)
            {
                RecordFailure(COPY_PML4_FAIL_INVALID_OLD_PD, static_cast<uint16_t>(PML4Index), static_cast<uint16_t>(PDPTIndex), 0xFFFF, PDPTEntry.value, OldPDAddr);
                return FailCopy();
            }

            PageTableEntry* OldPD = (PageTableEntry*) OldPDAddr;
            PageTableEntry* NewPD = (PageTableEntry*) NewPDAddr;

            for (UINTN PDIndex = 0; PDIndex < 512; PDIndex++)
            {
                PageTableEntry PDEntry = OldPD[PDIndex];
                if (!PDEntry.fields.present)
                {
                    continue;
                }

                if (PDEntry.fields.size)
                {
                    NewPD[PDIndex] = PDEntry;
                    continue;
                }

                ++AllocationAttempts;
                void* NewPTAddr = PMM.AllocatePagesFromDescriptor(1);
                if (NewPTAddr == NULL)
                {
                    RecordFailure(COPY_PML4_FAIL_ALLOC_PT, static_cast<uint16_t>(PML4Index), static_cast<uint16_t>(PDPTIndex), static_cast<uint16_t>(PDIndex),
                                  PDEntry.value, 0);
                    return FailCopy();
                }

                kmemset(NewPTAddr, 0, PAGE_SIZE);

                UINTN OldPTAddr       = (UINTN) (PDEntry.value & PHYS_PAGE_ADDR_MASK);
                if ((OldPTAddr & 0xFFFF000000000000ULL) != 0)
                {
                    RecordFailure(COPY_PML4_FAIL_INVALID_OLD_PT, static_cast<uint16_t>(PML4Index), static_cast<uint16_t>(PDPTIndex), static_cast<uint16_t>(PDIndex),
                                  PDEntry.value, OldPTAddr);
                    return FailCopy();
                }

                PageTableEntry* OldPT = (PageTableEntry*) OldPTAddr;
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

const CopyPageMapL4DebugInfo& VirtualMemoryManager::GetLastCopyPageMapL4DebugInfo() const
{
    return LastCopyPageMapL4DebugInfo;
}

const PageTableMutationDebugInfo& VirtualMemoryManager::GetLastPageTableMutationDebugInfo() const
{
    return LastPageTableMutationDebugInfo;
}
