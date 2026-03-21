/**
 * File: LogicLayer.cpp
 * Author: Marwan Mostafa
 * Description: Logic layer orchestration implementation.
 */

#include "LogicLayer.hpp"

#include "Layers/Resource/PartitionManager.hpp"
#include "Layers/Resource/ResourceLayer.hpp"
#include "Layers/Resource/TTY.hpp"

#include <Arch/x86.hpp>
#include <CommonUtils.hpp>
#include <Memory/VirtualMemoryManager.hpp>

namespace
{
constexpr uint64_t INITIAL_PROCESS_RFLAGS  = 0x202;
constexpr uint64_t USER_MODE_FLAG_MASK     = 0x3;
constexpr uint64_t STACK_ALIGNMENT_MASK    = 0xFULL;
constexpr uint64_t STACK_RETURN_SLOT_SIZE  = 8;
constexpr uint64_t EXECVE_MAX_VECTOR_COUNT = 128;
constexpr uint64_t AUXV_AT_NULL            = 0;
constexpr uint64_t AUXV_AT_PHDR            = 3;
constexpr uint64_t AUXV_AT_PHENT           = 4;
constexpr uint64_t AUXV_AT_PHNUM           = 5;
constexpr uint64_t AUXV_AT_PAGESZ          = 6;
constexpr uint64_t AUXV_AT_ENTRY           = 9;

bool TranslateELFFileOffsetToVirtualAddress(const ELFProgramHeader64* ProgramHeaders, uint16_t ProgramHeaderCount, uint64_t FileOffset, uint64_t* VirtualAddressOut)
{
    if (ProgramHeaders == nullptr || VirtualAddressOut == nullptr)
    {
        return false;
    }

    for (uint16_t ProgramHeaderIndex = 0; ProgramHeaderIndex < ProgramHeaderCount; ++ProgramHeaderIndex)
    {
        const ELFProgramHeader64& ProgramHeader = ProgramHeaders[ProgramHeaderIndex];
        if (!ProgramHeader.MemorySize || !ProgramHeader.FileSize)
        {
            continue;
        }

        if (ProgramHeader.Type != 1)
        {
            continue;
        }

        uint64_t SegmentFileStart = ProgramHeader.Offset;
        uint64_t SegmentFileEnd   = ProgramHeader.Offset + ProgramHeader.FileSize;
        if (SegmentFileEnd < SegmentFileStart)
        {
            continue;
        }

        if (FileOffset < SegmentFileStart || FileOffset >= SegmentFileEnd)
        {
            continue;
        }

        *VirtualAddressOut = ProgramHeader.VirtualAddress + (FileOffset - SegmentFileStart);
        return true;
    }

    return false;
}

/**
 * Function: AlignUpToPage
 * Description: Rounds a value up to the next page boundary.
 * Parameters:
 *   uint64_t Value - Value to align.
 * Returns:
 *   uint64_t - Page-aligned value rounded upward.
 */
uint64_t AlignUpToPage(uint64_t Value)
{
    return (Value + PAGE_SIZE - 1) & ~(static_cast<uint64_t>(PAGE_SIZE) - 1);
}

/**
 * Function: AlignDownToPage
 * Description: Rounds a value down to the nearest page boundary.
 * Parameters:
 *   uint64_t Value - Value to align.
 * Returns:
 *   uint64_t - Page-aligned value rounded downward.
 */
uint64_t AlignDownToPage(uint64_t Value)
{
    return Value & ~(static_cast<uint64_t>(PAGE_SIZE) - 1);
}

uint64_t LocalCStringLength(const char* String)
{
    if (String == nullptr)
    {
        return 0;
    }

    uint64_t Length = 0;
    while (String[Length] != '\0')
    {
        ++Length;
    }

    return Length;
}

void SetKernelSystemCallStackTop(uint64_t StackTop)
{
    if (StackTop == 0)
    {
        return;
    }

    KernelTSS.RSP0_lower = static_cast<uint32_t>(StackTop & 0xFFFFFFFF);
    KernelTSS.RSP0_upper = static_cast<uint32_t>((StackTop >> 32) & 0xFFFFFFFF);
}

bool InitializeExecveUserEntry(ResourceLayer* Resource, VirtualAddressSpace* AddressSpace, CpuState* State, const char* const* Argv, uint64_t Argc, const char* const* Envp, uint64_t Envc,
                               uint64_t AuxProgramHeaderAddress, uint64_t AuxProgramHeaderEntrySize, uint64_t AuxProgramHeaderCount, uint64_t AuxEntryPoint)
{
    if (Resource == nullptr || AddressSpace == nullptr || State == nullptr)
    {
        return false;
    }

    if ((Argc != 0 && Argv == nullptr) || (Envc != 0 && Envp == nullptr))
    {
        return false;
    }

    if (Argc > EXECVE_MAX_VECTOR_COUNT || Envc > EXECVE_MAX_VECTOR_COUNT)
    {
        return false;
    }

    uint64_t* ArgAddresses = nullptr;
    if (Argc != 0)
    {
        ArgAddresses = reinterpret_cast<uint64_t*>(Resource->kmalloc(sizeof(uint64_t) * Argc));
        if (ArgAddresses == nullptr)
        {
            return false;
        }
    }

    uint64_t* EnvAddresses = nullptr;
    if (Envc != 0)
    {
        EnvAddresses = reinterpret_cast<uint64_t*>(Resource->kmalloc(sizeof(uint64_t) * Envc));
        if (EnvAddresses == nullptr)
        {
            if (ArgAddresses != nullptr)
            {
                Resource->kfree(ArgAddresses);
            }

            return false;
        }
    }

    uint64_t UserPageTable     = AddressSpace->GetPageMapL4TableAddr();
    uint64_t PreviousPageTable = Resource->ReadCurrentPageTable();
    if (UserPageTable == 0 || PreviousPageTable == 0)
    {
        if (EnvAddresses != nullptr)
        {
            Resource->kfree(EnvAddresses);
        }

        if (ArgAddresses != nullptr)
        {
            Resource->kfree(ArgAddresses);
        }

        return false;
    }

    uint64_t StackBottom = AddressSpace->GetStackVirtualAddressStart();
    uint64_t StackTop    = AddressSpace->GetStackTop();
    uint64_t StackCursor = StackTop;

    bool Success = true;

    Resource->LoadPageTable(UserPageTable);

    do
    {
        for (uint64_t EnvIndex = Envc; EnvIndex > 0; --EnvIndex)
        {
            const char* EnvString = Envp[EnvIndex - 1];
            if (EnvString == nullptr)
            {
                Success = false;
                break;
            }

            uint64_t EnvLength = LocalCStringLength(EnvString) + 1;
            if (EnvLength > (StackCursor - StackBottom))
            {
                Success = false;
                break;
            }

            StackCursor -= EnvLength;
            memcpy(reinterpret_cast<void*>(StackCursor), EnvString, static_cast<size_t>(EnvLength));
            EnvAddresses[EnvIndex - 1] = StackCursor;
        }

        if (!Success)
        {
            break;
        }

        for (uint64_t ArgIndex = Argc; ArgIndex > 0; --ArgIndex)
        {
            const char* ArgString = Argv[ArgIndex - 1];
            if (ArgString == nullptr)
            {
                Success = false;
                break;
            }

            uint64_t ArgLength = LocalCStringLength(ArgString) + 1;
            if (ArgLength > (StackCursor - StackBottom))
            {
                Success = false;
                break;
            }

            StackCursor -= ArgLength;
            memcpy(reinterpret_cast<void*>(StackCursor), ArgString, static_cast<size_t>(ArgLength));
            ArgAddresses[ArgIndex - 1] = StackCursor;
        }

        if (!Success)
        {
            break;
        }

        StackCursor &= ~STACK_ALIGNMENT_MASK;

        uint64_t ArgvVectorCount = Argc + 1;
        uint64_t EnvpVectorCount = Envc + 1;
        uint64_t AuxvEntryCount  = 4; // AT_PAGESZ, optional AT_PHDR trio, optional AT_ENTRY, AT_NULL
        if (AuxProgramHeaderAddress != 0 && AuxProgramHeaderEntrySize != 0 && AuxProgramHeaderCount != 0)
        {
            AuxvEntryCount += 3;
        }
        if (AuxEntryPoint != 0)
        {
            AuxvEntryCount += 1;
        }

        uint64_t StackWordCount = 1 + ArgvVectorCount + EnvpVectorCount + (AuxvEntryCount * 2);
        uint64_t StackBytes     = StackWordCount * sizeof(uint64_t);

        if (StackBytes > (StackCursor - StackBottom))
        {
            Success = false;
            break;
        }

        StackCursor -= StackBytes;

        uint64_t ArgvUserPointer = StackCursor + sizeof(uint64_t);
        uint64_t EnvpUserPointer = ArgvUserPointer + (ArgvVectorCount * sizeof(uint64_t));
        uint64_t AuxvUserPointer = EnvpUserPointer + (EnvpVectorCount * sizeof(uint64_t));

        uint64_t NullPointer = 0;
        uint64_t WordOffset  = 0;

        memcpy(reinterpret_cast<void*>(StackCursor + (WordOffset * sizeof(uint64_t))), &Argc, sizeof(uint64_t));
        ++WordOffset;

        for (uint64_t ArgIndex = 0; ArgIndex < Argc; ++ArgIndex)
        {
            memcpy(reinterpret_cast<void*>(StackCursor + (WordOffset * sizeof(uint64_t))), &ArgAddresses[ArgIndex], sizeof(uint64_t));
            ++WordOffset;
        }

        memcpy(reinterpret_cast<void*>(StackCursor + (WordOffset * sizeof(uint64_t))), &NullPointer, sizeof(uint64_t));
        ++WordOffset;

        for (uint64_t EnvIndex = 0; EnvIndex < Envc; ++EnvIndex)
        {
            memcpy(reinterpret_cast<void*>(StackCursor + (WordOffset * sizeof(uint64_t))), &EnvAddresses[EnvIndex], sizeof(uint64_t));
            ++WordOffset;
        }

        memcpy(reinterpret_cast<void*>(StackCursor + (WordOffset * sizeof(uint64_t))), &NullPointer, sizeof(uint64_t));
        ++WordOffset;

        uint64_t AuxWordOffset = 0;

        auto PushAuxvPair = [&](uint64_t Type, uint64_t Value)
        {
            memcpy(reinterpret_cast<void*>(AuxvUserPointer + (AuxWordOffset * sizeof(uint64_t))), &Type, sizeof(uint64_t));
            ++AuxWordOffset;
            memcpy(reinterpret_cast<void*>(AuxvUserPointer + (AuxWordOffset * sizeof(uint64_t))), &Value, sizeof(uint64_t));
            ++AuxWordOffset;
        };

        PushAuxvPair(AUXV_AT_PAGESZ, PAGE_SIZE);

        if (AuxProgramHeaderAddress != 0 && AuxProgramHeaderEntrySize != 0 && AuxProgramHeaderCount != 0)
        {
            PushAuxvPair(AUXV_AT_PHDR, AuxProgramHeaderAddress);
            PushAuxvPair(AUXV_AT_PHENT, AuxProgramHeaderEntrySize);
            PushAuxvPair(AUXV_AT_PHNUM, AuxProgramHeaderCount);
        }

        if (AuxEntryPoint != 0)
        {
            PushAuxvPair(AUXV_AT_ENTRY, AuxEntryPoint);
        }

        PushAuxvPair(AUXV_AT_NULL, 0);

        State->rsp = StackCursor;
        State->rdi = Argc;
        State->rsi = ArgvUserPointer;
        State->rdx = EnvpUserPointer;

#ifdef DEBUG_BUILD
        if (Resource->GetTTY() != nullptr)
        {
            uint64_t StackArgc         = 0;
            uint64_t StackArgv0Pointer = 0;
            memcpy(&StackArgc, reinterpret_cast<const void*>(StackCursor), sizeof(StackArgc));
            memcpy(&StackArgv0Pointer, reinterpret_cast<const void*>(StackCursor + sizeof(uint64_t)), sizeof(StackArgv0Pointer));

            char StackArgv0[64] = {};
            if (StackArgv0Pointer >= StackBottom && StackArgv0Pointer < StackTop)
            {
                uint64_t MaxReadable = StackTop - StackArgv0Pointer;
                uint64_t Limit       = (MaxReadable < (sizeof(StackArgv0) - 1)) ? MaxReadable : (sizeof(StackArgv0) - 1);
                for (uint64_t Index = 0; Index < Limit; ++Index)
                {
                    char CurrentChar  = *(reinterpret_cast<const char*>(StackArgv0Pointer + Index));
                    StackArgv0[Index] = CurrentChar;
                    if (CurrentChar == '\0')
                    {
                        break;
                    }
                }
                StackArgv0[sizeof(StackArgv0) - 1] = '\0';
            }
            else
            {
                memcpy(StackArgv0, "<out-of-stack>", sizeof("<out-of-stack>"));
            }

            Resource->GetTTY()->Serialprintf("exec_stack_dbg: rsp=%p argc=%lu argv0_ptr=%p argv0='%s'\n", (void*) StackCursor, StackArgc, (void*) StackArgv0Pointer, StackArgv0);
        }
#endif
    } while (false);

    Resource->LoadPageTable(PreviousPageTable);

    if (EnvAddresses != nullptr)
    {
        Resource->kfree(EnvAddresses);
    }

    if (ArgAddresses != nullptr)
    {
        Resource->kfree(ArgAddresses);
    }

    return Success;
}

bool IsRangeWithin(uint64_t RangeStart, uint64_t RangeSize, uint64_t Address, uint64_t Count)
{
    if (RangeSize == 0)
    {
        return false;
    }

    if (Address < RangeStart)
    {
        return false;
    }

    uint64_t RangeEnd = RangeStart + RangeSize;
    if (RangeEnd < RangeStart)
    {
        return false;
    }

    uint64_t AccessEnd = Address + Count;
    if (AccessEnd < Address)
    {
        return false;
    }

    return AccessEnd <= RangeEnd;
}

bool IsRangeWithinAnyELFRegion(const VirtualAddressSpaceELF* ELFAddressSpace, uint64_t Address, uint64_t Count)
{
    if (ELFAddressSpace == nullptr)
    {
        return false;
    }

    const ELFMemoryRegion* Regions     = ELFAddressSpace->GetMemoryRegions();
    size_t                 RegionCount = ELFAddressSpace->GetMemoryRegionCount();

    for (size_t RegionIndex = 0; RegionIndex < RegionCount; ++RegionIndex)
    {
        const ELFMemoryRegion& Region = Regions[RegionIndex];
        if (Region.Size == 0)
        {
            continue;
        }

        uint64_t RegionStart = AlignDownToPage(Region.VirtualAddress);
        uint64_t RegionEnd   = AlignUpToPage(Region.VirtualAddress + Region.Size);
        if (RegionEnd <= RegionStart)
        {
            continue;
        }

        if (IsRangeWithin(RegionStart, RegionEnd - RegionStart, Address, Count))
        {
            return true;
        }
    }

    return false;
}

bool IsRangeWithinAnyProcessMapping(const Process* CurrentProcess, uint64_t Address, uint64_t Count)
{
    if (CurrentProcess == nullptr)
    {
        return false;
    }

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        const ProcessMemoryMapping& Mapping = CurrentProcess->MemoryMappings[MappingIndex];
        if (!Mapping.InUse)
        {
            continue;
        }

        if (IsRangeWithin(Mapping.VirtualAddressStart, Mapping.Length, Address, Count))
        {
            return true;
        }
    }

    return false;
}

bool IsLowerCanonicalUserAddress(uint64_t Address)
{
    constexpr uint64_t LOWER_CANONICAL_MAX = 0x00007FFFFFFFFFFFULL;
    return Address <= LOWER_CANONICAL_MAX;
}

bool IsLowerCanonicalUserRange(uint64_t Address, uint64_t Count)
{
    if (Count == 0)
    {
        return IsLowerCanonicalUserAddress(Address);
    }

    uint64_t EndInclusive = Address + (Count - 1);
    if (EndInclusive < Address)
    {
        return false;
    }

    return IsLowerCanonicalUserAddress(Address) && IsLowerCanonicalUserAddress(EndInclusive);
}

bool IsUserAccessiblePageMapped(uint64_t PageMapL4TableAddr, uint64_t Address)
{
    if (PageMapL4TableAddr == 0)
    {
        return false;
    }

    VirtualAddress Vaddr;
    Vaddr.value = Address;

    PageTableEntry* PML4 = reinterpret_cast<PageTableEntry*>(PageMapL4TableAddr);
    if (PML4 == nullptr)
    {
        return false;
    }

    PageTableEntry PML4Entry = PML4[Vaddr.fields.pml4_index];
    if (!PML4Entry.fields.present || !PML4Entry.fields.user_access)
    {
        return false;
    }

    PageTableEntry* PDPT = reinterpret_cast<PageTableEntry*>(PML4Entry.value & PHYS_PAGE_ADDR_MASK);
    if (PDPT == nullptr)
    {
        return false;
    }

    PageTableEntry PDPTEntry = PDPT[Vaddr.fields.pdpt_index];
    if (!PDPTEntry.fields.present || !PDPTEntry.fields.user_access)
    {
        return false;
    }

    PageTableEntry* PD = reinterpret_cast<PageTableEntry*>(PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
    if (PD == nullptr)
    {
        return false;
    }

    PageTableEntry PDEntry = PD[Vaddr.fields.pd_index];
    if (!PDEntry.fields.present || !PDEntry.fields.user_access)
    {
        return false;
    }

    if (PDEntry.fields.size)
    {
        return true;
    }

    PageTableEntry* PT = reinterpret_cast<PageTableEntry*>(PDEntry.value & PHYS_PAGE_ADDR_MASK);
    if (PT == nullptr)
    {
        return false;
    }

    PageTableEntry PTEntry = PT[Vaddr.fields.pt_index];
    if (!PTEntry.fields.present || !PTEntry.fields.user_access)
    {
        return false;
    }

    return true;
}

bool IsUserAddressRangeMappedByPageTables(const Process* CurrentProcess, uint64_t Address, uint64_t Count)
{
    if (CurrentProcess == nullptr || CurrentProcess->AddressSpace == nullptr)
    {
        return false;
    }

    if (Count == 0)
    {
        return true;
    }

    uint64_t PageMapL4TableAddr = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
    if (PageMapL4TableAddr == 0)
    {
        return false;
    }

    uint64_t StartPage = AlignDownToPage(Address);
    uint64_t EndPage   = AlignDownToPage(Address + (Count - 1));

    for (uint64_t PageAddress = StartPage; PageAddress <= EndPage; PageAddress += PAGE_SIZE)
    {
        if (!IsUserAccessiblePageMapped(PageMapL4TableAddr, PageAddress))
        {
            return false;
        }

        if (PageAddress > (UINT64_MAX - PAGE_SIZE))
        {
            break;
        }
    }

    return true;
}

void FreeAnonymousProcessMappings(PhysicalMemoryManager* PMM, Process* TargetProcess)
{
    if (PMM == nullptr || TargetProcess == nullptr)
    {
        return;
    }

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        ProcessMemoryMapping& Mapping = TargetProcess->MemoryMappings[MappingIndex];
        if (!Mapping.InUse)
        {
            continue;
        }

        if (Mapping.IsAnonymous && Mapping.PhysicalAddressStart != 0 && Mapping.Length != 0)
        {
            uint64_t PageCount = (Mapping.Length + PAGE_SIZE - 1) / PAGE_SIZE;
            PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(Mapping.PhysicalAddressStart), PageCount);
        }

        Mapping = {};
    }
}

bool CloneAnonymousProcessMappings(ResourceLayer* Resource, const Process* SourceProcess, Process* TargetProcess)
{
    if (Resource == nullptr || SourceProcess == nullptr || TargetProcess == nullptr || SourceProcess->AddressSpace == nullptr || TargetProcess->AddressSpace == nullptr)
    {
        return false;
    }

    PhysicalMemoryManager* PMM = Resource->GetPMM();
    if (PMM == nullptr)
    {
        return false;
    }

    uint64_t TargetPageTable = TargetProcess->AddressSpace->GetPageMapL4TableAddr();
    if (TargetPageTable == 0)
    {
        return false;
    }

    VirtualMemoryManager TargetVMM(TargetPageTable, *PMM);

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        const ProcessMemoryMapping& SourceMapping = SourceProcess->MemoryMappings[MappingIndex];
        if (!SourceMapping.InUse)
        {
            continue;
        }

        ProcessMemoryMapping ClonedMapping = SourceMapping;

        if (SourceMapping.IsAnonymous)
        {
            if (SourceMapping.PhysicalAddressStart == 0 || SourceMapping.Length == 0)
            {
                return false;
            }

            uint64_t PageCount = (SourceMapping.Length + PAGE_SIZE - 1) / PAGE_SIZE;
            void*    NewPages  = PMM->AllocatePagesFromDescriptor(PageCount);
            if (NewPages == nullptr)
            {
                return false;
            }

            memcpy(NewPages, reinterpret_cast<const void*>(SourceMapping.PhysicalAddressStart), static_cast<size_t>(PageCount * PAGE_SIZE));

            bool MappingSucceeded = true;
            for (uint64_t PageIndex = 0; PageIndex < PageCount; ++PageIndex)
            {
                uint64_t PhysicalPage = reinterpret_cast<uint64_t>(NewPages) + (PageIndex * PAGE_SIZE);
                uint64_t VirtualPage  = SourceMapping.VirtualAddressStart + (PageIndex * PAGE_SIZE);
                if (!TargetVMM.MapPage(PhysicalPage, VirtualPage, PageMappingFlags(true, true)))
                {
                    MappingSucceeded = false;
                    for (uint64_t RollbackIndex = 0; RollbackIndex < PageIndex; ++RollbackIndex)
                    {
                        uint64_t RollbackVirtualPage = SourceMapping.VirtualAddressStart + (RollbackIndex * PAGE_SIZE);
                        TargetVMM.UnmapPage(RollbackVirtualPage);
                    }

                    PMM->FreePagesFromDescriptor(NewPages, PageCount);
                    break;
                }
            }

            if (!MappingSucceeded)
            {
                return false;
            }

            ClonedMapping.PhysicalAddressStart = reinterpret_cast<uint64_t>(NewPages);
        }

        TargetProcess->MemoryMappings[MappingIndex] = ClonedMapping;
    }

    return true;
}

bool IsUserAddressRangeAccessible(const Process* CurrentProcess, uint64_t Address, uint64_t Count)
{
    if (CurrentProcess == nullptr)
    {
        return false;
    }

    if (Count == 0)
    {
        return true;
    }

    if (CurrentProcess->Level != PROCESS_LEVEL_USER || CurrentProcess->AddressSpace == nullptr)
    {
        return false;
    }

    if (!IsLowerCanonicalUserRange(Address, Count))
    {
        return false;
    }

    const VirtualAddressSpace* AddressSpace = CurrentProcess->AddressSpace;

    if (CurrentProcess->FileType == FILE_TYPE_ELF)
    {
        const VirtualAddressSpaceELF* ELFAddressSpace = static_cast<const VirtualAddressSpaceELF*>(AddressSpace);
        if (IsRangeWithinAnyELFRegion(ELFAddressSpace, Address, Count))
        {
            return true;
        }
    }
    else if (IsRangeWithin(AddressSpace->GetCodeVirtualAddressStart(), AddressSpace->GetCodeSize(), Address, Count))
    {
        return true;
    }

    if (IsRangeWithin(AddressSpace->GetHeapVirtualAddressStart(), AddressSpace->GetHeapSize(), Address, Count))
    {
        return true;
    }

    if (IsRangeWithin(AddressSpace->GetStackVirtualAddressStart(), AddressSpace->GetStackSize(), Address, Count))
    {
        return true;
    }

    if (IsRangeWithinAnyProcessMapping(CurrentProcess, Address, Count))
    {
        return true;
    }

    return IsUserAddressRangeMappedByPageTables(CurrentProcess, Address, Count);
}

void FreePageTableHierarchy(PhysicalMemoryManager* PMM, uint64_t PageMapL4TableAddr)
{
    if (PMM == nullptr || PageMapL4TableAddr == 0)
    {
        return;
    }

    PageTableEntry* PML4 = reinterpret_cast<PageTableEntry*>(PageMapL4TableAddr);

    for (uint64_t PML4Index = 0; PML4Index < 512; ++PML4Index)
    {
        PageTableEntry PML4Entry = PML4[PML4Index];
        if (!PML4Entry.fields.present)
        {
            continue;
        }

        PageTableEntry* PDPT = reinterpret_cast<PageTableEntry*>(PML4Entry.value & PHYS_PAGE_ADDR_MASK);
        if (PDPT == nullptr)
        {
            continue;
        }

        for (uint64_t PDPTIndex = 0; PDPTIndex < 512; ++PDPTIndex)
        {
            PageTableEntry PDPTEntry = PDPT[PDPTIndex];
            if (!PDPTEntry.fields.present)
            {
                continue;
            }

            PageTableEntry* PD = reinterpret_cast<PageTableEntry*>(PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
            if (PD == nullptr)
            {
                continue;
            }

            for (uint64_t PDIndex = 0; PDIndex < 512; ++PDIndex)
            {
                PageTableEntry PDEntry = PD[PDIndex];
                if (!PDEntry.fields.present)
                {
                    continue;
                }

                PageTableEntry* PT = reinterpret_cast<PageTableEntry*>(PDEntry.value & PHYS_PAGE_ADDR_MASK);
                if (PT != nullptr)
                {
                    PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(PT), 1);
                }
            }

            PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(PD), 1);
        }

        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(PDPT), 1);
    }

    PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(PageMapL4TableAddr), 1);
}
} // namespace

namespace
{
/**
 * Function: NullProcessEntry
 * Description: Idle process entry point that halts CPU in an infinite loop.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void NullProcessEntry()
{
    while (1)
        X86Halt();
}
} // namespace

/**
 * Function: LogicLayer::LogicLayer
 * Description: Constructs logic layer with uninitialized subsystem pointers.
 * Parameters:
 *   None
 * Returns:
 *   LogicLayer - Constructed logic layer instance.
 */
LogicLayer::LogicLayer() : Resource(nullptr), PM(nullptr), Sched(nullptr), Sync(nullptr), ELF(nullptr)
{
}

/**
 * Function: LogicLayer::~LogicLayer
 * Description: Destroys logic layer and releases owned subsystem objects.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
LogicLayer::~LogicLayer()
{
    if (PM != nullptr)
    {
        delete PM;
    }

    if (Sched != nullptr)
    {
        delete Sched;
    }

    if (Sync != nullptr)
    {
        delete Sync;
    }

    if (ELF != nullptr)
    {
        delete ELF;
    }

    if (VFS != nullptr)
    {
        delete VFS;
    }
}

/**
 * Function: LogicLayer::Initialize
 * Description: Sets resource-layer dependency used by logic operations.
 * Parameters:
 *   ResourceLayer* Resource - Resource layer instance.
 * Returns:
 *   void - No return value.
 */
void LogicLayer::Initialize(ResourceLayer* Resource)
{
    this->Resource = Resource;
}

/**
 * Function: LogicLayer::GetResourceLayer
 * Description: Returns attached resource layer.
 * Parameters:
 *   None
 * Returns:
 *   ResourceLayer* - Pointer to resource layer.
 */
ResourceLayer* LogicLayer::GetResourceLayer() const
{
    return Resource;
}

/**
 * Function: LogicLayer::GetELFManager
 * Description: Returns ELF manager instance.
 * Parameters:
 *   None
 * Returns:
 *   ELFManager* - Pointer to ELF manager.
 */
ELFManager* LogicLayer::GetELFManager() const
{
    return ELF;
}

/**
 * Function: LogicLayer::GetProcessManager
 * Description: Returns process manager instance.
 * Parameters:
 *   None
 * Returns:
 *   ProcessManager* - Pointer to process manager.
 */
ProcessManager* LogicLayer::GetProcessManager() const
{
    return PM;
}

/**
 * Function: LogicLayer::GetVirtualFileSystem
 * Description: Returns virtual file system instance.
 * Parameters:
 *   None
 * Returns:
 *   VirtualFileSystem* - Pointer to virtual file system.
 */
VirtualFileSystem* LogicLayer::GetVirtualFileSystem() const
{
    return VFS;
}

/**
 * Function: LogicLayer::kmalloc
 * Description: Allocates kernel memory through the resource layer allocator.
 * Parameters:
 *   uint64_t Size - Number of bytes to allocate.
 * Returns:
 *   void* - Pointer to allocated memory or nullptr when unavailable.
 */
void* LogicLayer::kmalloc(uint64_t Size)
{
    if (Resource == nullptr)
    {
        return nullptr;
    }

    return Resource->kmalloc(Size);
}

/**
 * Function: LogicLayer::kfree
 * Description: Frees previously allocated kernel memory through the resource layer allocator.
 * Parameters:
 *   void* Pointer - Pointer to memory to free.
 * Returns:
 *   void - No return value.
 */
void LogicLayer::kfree(void* Pointer)
{
    if (Resource == nullptr || Pointer == nullptr)
    {
        return;
    }

    Resource->kfree(Pointer);
}

/**
 * Function: LogicLayer::InitializeProcessManager
 * Description: Creates process manager if it has not been initialized.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void LogicLayer::InitializeProcessManager()
{
    if (PM != nullptr)
    {
        return;
    }

    PM = new ProcessManager();
    Resource->GetTTY()->printf_("Process Manager Initialized\n");
}

/**
 * Function: LogicLayer::InitializeScheduler
 * Description: Creates scheduler if it has not been initialized.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void LogicLayer::InitializeScheduler()
{
    if (Sched != nullptr)
    {
        return;
    }

    Sched = new Scheduler();
    Resource->GetTTY()->printf_("Scheduler Initialized\n");
}

/**
 * Function: LogicLayer::InitializeSynchronizationManager
 * Description: Creates synchronization manager if it has not been initialized.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void LogicLayer::InitializeSynchronizationManager()
{
    if (Sync != nullptr)
    {
        return;
    }

    Sync = new SynchronizationManager();
    Resource->GetTTY()->printf_("Synchronization Manager Initialized\n");
}

/**
 * Function: LogicLayer::InitializeELFManager
 * Description: Creates ELF manager if it has not been initialized.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void LogicLayer::InitializeELFManager()
{
    if (ELF != nullptr)
    {
        return;
    }

    ELF = new ELFManager();
    Resource->GetTTY()->printf_("ELF Manager Initialized\n");
}

/**
 * Function: LogicLayer::InitializeVirtualFileSystem
 * Description: Creates virtual file system if it has not been initialized.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void LogicLayer::InitializeVirtualFileSystem()
{
    if (VFS != nullptr)
    {
        return;
    }

    VFS = new VirtualFileSystem();
    Resource->GetTTY()->printf_("Virtual File System Initialized\n");
}

bool LogicLayer::RegisterPartitionDevices()
{
    if (Resource == nullptr || VFS == nullptr)
    {
        return false;
    }

    DeviceManager*    DeviceManagerInstance    = Resource->GetDeviceManager();
    PartitionManager* PartitionManagerInstance = Resource->GetPartitionManager();
    if (DeviceManagerInstance == nullptr || PartitionManagerInstance == nullptr)
    {
        return false;
    }

    return PartitionManagerInstance->RegisterPartitionDevices(DeviceManagerInstance, VFS);
}

bool LogicLayer::InitializeExtendedFileSystem(const char* DevicePath, const char* MountLocation)
{
    if (Resource == nullptr || VFS == nullptr)
    {
        return false;
    }

    PartitionManager* PartitionManagerInstance = Resource->GetPartitionManager();
    TTY*              Terminal                 = Resource->GetTTY();
    if (PartitionManagerInstance == nullptr || DevicePath == nullptr || MountLocation == nullptr)
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("ext filesystem init failed: invalid initialization state\n");
        }
        return false;
    }

    RootFileSystemPartitionInfo RootPartitionInfo = {};
    if (!PartitionManagerInstance->GetPartitionByDevicePath(DevicePath, &RootPartitionInfo))
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("ext filesystem partition not found: %s\n", DevicePath);
        }
        return false;
    }

    if (!Resource->InitializeExtendedFileSystemManager(&RootPartitionInfo))
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("ext filesystem init failed: %s\n", DevicePath);
        }
        return false;
    }

    if (!VFS->MountEXTFileSystem(Resource->GetExtendedFileSystemManager(), MountLocation))
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("ext filesystem mount failed: device=%s mount=%s\n", DevicePath, MountLocation);
        }
        return false;
    }

    return true;
}

/**
 * Function: LogicLayer::CreateNullProcess
 * Description: Creates idle kernel process and marks it running for initial scheduling.
 * Parameters:
 *   None
 * Returns:
 *   uint8_t - Process ID of created null process.
 */
uint8_t LogicLayer::CreateNullProcess()
{
    uint8_t Id = CreateKernelProcess(NullProcessEntry);

    Process* NullProcess = PM->GetProcessById(Id);
    NullProcess->Status  = PROCESS_RUNNING; // So RunProcess works on first schedule

    return Id;
}

/**
 * Function: LogicLayer::CreateKernelProcess
 * Description: Creates a kernel process with prepared initial CPU state and stack.
 * Parameters:
 *   void (*EntryPoint)() - Kernel function entry point.
 * Returns:
 *   uint8_t - Process ID or 0xFF on failure.
 */
uint8_t LogicLayer::CreateKernelProcess(void (*EntryPoint)())
{
    void* ProcessStack = Resource->kmalloc(KERNEL_PROCESS_STACK_SIZE);
    if (ProcessStack == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    uint64_t StackTop = reinterpret_cast<uint64_t>(ProcessStack) + KERNEL_PROCESS_STACK_SIZE;

    CpuState State = {};
    State.rip      = reinterpret_cast<uint64_t>(EntryPoint);
    State.rflags   = INITIAL_PROCESS_RFLAGS;                                      // Bit1 always set, IF enabled
    State.rbp      = 0;                                                           // bottom of stack frame
    State.rsp      = (StackTop & ~STACK_ALIGNMENT_MASK) - STACK_RETURN_SLOT_SIZE; // SysV entry alignment without a real CALL

    *reinterpret_cast<uint64_t*>(State.rsp) = 0;

    State.cs   = KERNEL_CS;
    State.ss   = KERNEL_SS;
    uint8_t Id = PM->CreateKernelProcess(ProcessStack, State);

    if (Id != PROCESS_ID_INVALID)
    {
        Sched->AddToReadyQueue(Id);
    }
    else
    {
        Resource->kfree(ProcessStack);
    }

    return Id;
}

uint8_t LogicLayer::ChangeProcessExecution(uint8_t Id, const char* FilePath, const char* const* Argv, uint64_t Argc, const char* const* Envp, uint64_t Envc)
{
    if (PM == nullptr || VFS == nullptr || Resource == nullptr || FilePath == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status == PROCESS_TERMINATED)
    {
        return PROCESS_ID_INVALID;
    }

    if (TargetProcess->Level != PROCESS_LEVEL_USER)
    {
        return PROCESS_ID_INVALID;
    }

    Dentry* Entry = VFS->Lookup(FilePath);
    if (Entry == nullptr || Entry->inode == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    if (Entry->inode->NodeType != INODE_FILE)
    {
        return PROCESS_ID_INVALID;
    }

    if (Entry->inode->NodeData == nullptr || Entry->inode->NodeSize == 0)
    {
        return PROCESS_ID_INVALID;
    }

    PhysicalMemoryManager* PMM = Resource->GetPMM();
    if (PMM == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    uint64_t CodeSize = Entry->inode->NodeSize;
    uint64_t Pages    = (CodeSize + PAGE_SIZE - 1) / PAGE_SIZE;

    void* CopiedImage = PMM->AllocatePagesFromDescriptor(Pages);
    if (CopiedImage == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    kmemset(CopiedImage, 0, Pages * PAGE_SIZE);
    memcpy(CopiedImage, Entry->inode->NodeData, static_cast<size_t>(CodeSize));

    ELFHeader            Header                    = {};
    VirtualAddressSpace* NewAddressSpace           = nullptr;
    bool                 IsELF                     = false;
    uint64_t             NewUserEntryPoint         = USER_PROCESS_VIRTUAL_BASE;
    uint64_t             AuxProgramHeaderAddress   = 0;
    uint64_t             AuxProgramHeaderEntrySize = 0;
    uint64_t             AuxProgramHeaderCount     = 0;
    uint64_t             AuxEntryPoint             = 0;

    if (ELF != nullptr)
    {
        Header = ELF->ParseELF(reinterpret_cast<uint64_t>(CopiedImage));
        IsELF  = ELF->ValidateELF(Header);
    }

    if (IsELF)
    {
        const ELFProgramHeader64* ProgramHeaders = ELF->GetProgramHeaderTable(reinterpret_cast<uint64_t>(CopiedImage), Header);
        if (ProgramHeaders != nullptr)
        {
            TranslateELFFileOffsetToVirtualAddress(ProgramHeaders, Header.ProgramHeaderEntryCount, Header.ProgramHeaderOffset, &AuxProgramHeaderAddress);
        }

        AuxProgramHeaderEntrySize = Header.ProgramHeaderEntrySize;
        AuxProgramHeaderCount     = Header.ProgramHeaderEntryCount;
        AuxEntryPoint             = Header.Entry;

        NewAddressSpace   = MapELF(reinterpret_cast<uint64_t>(CopiedImage), CodeSize, Header);
        NewUserEntryPoint = Header.Entry;
    }
    else
    {
        NewAddressSpace = MapRawBinary(reinterpret_cast<uint64_t>(CopiedImage), CodeSize);
    }

    if (NewAddressSpace == nullptr)
    {
        PMM->FreePagesFromDescriptor(CopiedImage, Pages);
        return PROCESS_ID_INVALID;
    }

    CpuState NewState = {};
    NewState.rip      = NewUserEntryPoint;
    NewState.rflags   = INITIAL_PROCESS_RFLAGS;
    NewState.rbp      = 0;
    NewState.cs       = USER_CS;
    NewState.ss       = USER_SS;

    if (!InitializeExecveUserEntry(Resource, NewAddressSpace, &NewState, Argv, Argc, Envp, Envc, AuxProgramHeaderAddress, AuxProgramHeaderEntrySize, AuxProgramHeaderCount, AuxEntryPoint))
    {
        if (IsELF)
        {
            CleanUpELF(NewAddressSpace);
        }
        else
        {
            CleanUpRawBinary(NewAddressSpace);
        }

        delete NewAddressSpace;
        return PROCESS_ID_INVALID;
    }

    VirtualAddressSpace* OldAddressSpace             = TargetProcess->AddressSpace;
    FILE_TYPE            OldFileType                 = TargetProcess->FileType;
    bool                 SkipOldAddressSpaceCleanup  = false;
    bool                 SkipAnonymousMappingCleanup = false;

    if (TargetProcess->IsVforkChild && TargetProcess->VforkParentId != PROCESS_ID_INVALID)
    {
        Process* ParentProcess = PM->GetProcessById(TargetProcess->VforkParentId);
        if (ParentProcess != nullptr && ParentProcess->AddressSpace == OldAddressSpace)
        {
            SkipOldAddressSpaceCleanup  = true;
            SkipAnonymousMappingCleanup = true;
        }
    }

    if (!SkipAnonymousMappingCleanup)
    {
        if (!SkipAnonymousMappingCleanup)
        {
            FreeAnonymousProcessMappings(Resource->GetPMM(), TargetProcess);
        }
    }
    else
    {
        for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
        {
            TargetProcess->MemoryMappings[MappingIndex] = {};
        }
    }

    TargetProcess->AddressSpace = NewAddressSpace;
    TargetProcess->FileType     = IsELF ? FILE_TYPE_ELF : FILE_TYPE_RAW_BINARY;
    TargetProcess->StackPointer = reinterpret_cast<void*>(NewAddressSpace->GetStackVirtualAddressStart());
    TargetProcess->State        = NewState;

    Process* RunningProcess = PM->GetRunningProcess();
    if (RunningProcess == TargetProcess)
    {
        uint64_t NewPageTable = NewAddressSpace->GetPageMapL4TableAddr();
        if (NewPageTable != 0)
        {
            Resource->LoadPageTable(NewPageTable);
        }
        PM->UpdateCurrentProcessId(TargetProcess->Id);
    }

    if (OldAddressSpace != nullptr && !SkipOldAddressSpaceCleanup)
    {
        if (OldFileType == FILE_TYPE_ELF)
        {
            CleanUpELF(OldAddressSpace);
        }
        else
        {
            CleanUpRawBinary(OldAddressSpace);
        }

        delete OldAddressSpace;
    }

    return Id;
}

/**
 * Function: LogicLayer::CopyProcess
 * Description: Creates a fork-like copy of an existing user process and records the parent process ID.
 * Parameters:
 *   uint8_t Id - Source process ID to clone.
 * Returns:
 *   uint8_t - New child process ID or 0xFF on failure.
 */
uint8_t LogicLayer::CopyProcess(uint8_t Id)
{
    if (PM == nullptr || Sched == nullptr || Resource == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    Process* SourceProcess = PM->GetProcessById(Id);
    if (SourceProcess == nullptr || SourceProcess->Status == PROCESS_TERMINATED || SourceProcess->Level != PROCESS_LEVEL_USER || SourceProcess->AddressSpace == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    PhysicalMemoryManager* PMM = Resource->GetPMM();
    if (PMM == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    VirtualAddressSpace* SourceAddressSpace = SourceProcess->AddressSpace;
    uint64_t             SourceCodeSize     = SourceAddressSpace->GetCodeSize();
    uint64_t             SourceCodePhysAddr = SourceAddressSpace->GetCodePhysicalAddress();

    if (SourceCodeSize == 0 || SourceCodePhysAddr == 0)
    {
        return PROCESS_ID_INVALID;
    }

    uint64_t SourceCodePages = (SourceCodeSize + PAGE_SIZE - 1) / PAGE_SIZE;
    void*    CopiedImage     = PMM->AllocatePagesFromDescriptor(SourceCodePages);
    if (CopiedImage == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    kmemset(CopiedImage, 0, SourceCodePages * PAGE_SIZE);
    memcpy(CopiedImage, reinterpret_cast<void*>(SourceCodePhysAddr), static_cast<size_t>(SourceCodeSize));

    VirtualAddressSpace* NewAddressSpace = nullptr;

    if (SourceProcess->FileType == FILE_TYPE_ELF)
    {
        if (ELF == nullptr)
        {
            PMM->FreePagesFromDescriptor(CopiedImage, SourceCodePages);
            return PROCESS_ID_INVALID;
        }

        ELFHeader Header = ELF->ParseELF(reinterpret_cast<uint64_t>(CopiedImage));
        if (!ELF->ValidateELF(Header))
        {
            PMM->FreePagesFromDescriptor(CopiedImage, SourceCodePages);
            return PROCESS_ID_INVALID;
        }

        NewAddressSpace = MapELF(reinterpret_cast<uint64_t>(CopiedImage), SourceCodeSize, Header, static_cast<VirtualAddressSpaceELF*>(SourceAddressSpace));
    }
    else
    {
        NewAddressSpace = MapRawBinary(reinterpret_cast<uint64_t>(CopiedImage), SourceCodeSize);
    }

    if (NewAddressSpace == nullptr)
    {
        PMM->FreePagesFromDescriptor(CopiedImage, SourceCodePages);
        return PROCESS_ID_INVALID;
    }

    uint64_t SourceHeapPhysAddr = SourceAddressSpace->GetHeapPhysicalAddress();
    uint64_t SourceHeapSize     = SourceAddressSpace->GetHeapSize();
    uint64_t ChildHeapPhysAddr  = NewAddressSpace->GetHeapPhysicalAddress();
    uint64_t ChildHeapSize      = NewAddressSpace->GetHeapSize();

    uint64_t HeapBytesToCopy = (SourceHeapSize < ChildHeapSize) ? SourceHeapSize : ChildHeapSize;
    if (SourceHeapPhysAddr != 0 && ChildHeapPhysAddr != 0 && HeapBytesToCopy != 0)
    {
        memcpy(reinterpret_cast<void*>(ChildHeapPhysAddr), reinterpret_cast<void*>(SourceHeapPhysAddr), static_cast<size_t>(HeapBytesToCopy));
    }

    uint64_t SourceStackPhysAddr = SourceAddressSpace->GetStackPhysicalAddress();
    uint64_t SourceStackSize     = SourceAddressSpace->GetStackSize();
    uint64_t ChildStackPhysAddr  = NewAddressSpace->GetStackPhysicalAddress();
    uint64_t ChildStackSize      = NewAddressSpace->GetStackSize();

    uint64_t StackBytesToCopy = (SourceStackSize < ChildStackSize) ? SourceStackSize : ChildStackSize;
    if (SourceStackPhysAddr != 0 && ChildStackPhysAddr != 0 && StackBytesToCopy != 0)
    {
        memcpy(reinterpret_cast<void*>(ChildStackPhysAddr), reinterpret_cast<void*>(SourceStackPhysAddr), static_cast<size_t>(StackBytesToCopy));
    }

    CpuState ChildState = SourceProcess->State;
    ChildState.rax      = 0;

    uint8_t ChildId = PM->CreateUserProcess(reinterpret_cast<void*>(NewAddressSpace->GetStackVirtualAddressStart()), ChildState, NewAddressSpace, SourceProcess->FileType);
    if (ChildId == PROCESS_ID_INVALID)
    {
        if (SourceProcess->FileType == FILE_TYPE_ELF)
        {
            CleanUpELF(NewAddressSpace);
        }
        else
        {
            CleanUpRawBinary(NewAddressSpace);
        }

        delete NewAddressSpace;
        return PROCESS_ID_INVALID;
    }

    Process* ChildProcess = PM->GetProcessById(ChildId);
    if (ChildProcess == nullptr)
    {
        KillProcess(ChildId);
        return PROCESS_ID_INVALID;
    }

    ChildProcess->ParrentId                 = SourceProcess->Id;
    ChildProcess->UserFSBase                = SourceProcess->UserFSBase;
    ChildProcess->BlockedSignalMask         = SourceProcess->BlockedSignalMask;
    ChildProcess->ClearChildTidAddress      = SourceProcess->ClearChildTidAddress;
    ChildProcess->ProgramBreak              = SourceProcess->ProgramBreak;
    ChildProcess->CurrentFileSystemLocation = SourceProcess->CurrentFileSystemLocation;

    if (!CloneAnonymousProcessMappings(Resource, SourceProcess, ChildProcess))
    {
        KillProcess(ChildId);
        return PROCESS_ID_INVALID;
    }

    for (size_t SignalIndex = 0; SignalIndex < MAX_POSIX_SIGNALS_PER_PROCESS; ++SignalIndex)
    {
        ChildProcess->SignalActions[SignalIndex] = SourceProcess->SignalActions[SignalIndex];
    }

    for (size_t FileIndex = 0; FileIndex < MAX_OPEN_FILES_PER_PROCESS; ++FileIndex)
    {
        if (SourceProcess->FileTable[FileIndex] == nullptr)
        {
            continue;
        }

        File* CopiedFile = new File;
        if (CopiedFile == nullptr)
        {
            KillProcess(ChildId);
            return PROCESS_ID_INVALID;
        }

        *CopiedFile                        = *SourceProcess->FileTable[FileIndex];
        ChildProcess->FileTable[FileIndex] = CopiedFile;
    }

    Sched->AddToReadyQueue(ChildId);
    return ChildId;
}

/**
 * Function: LogicLayer::CreateUserProcessFromVFS
 * Description: Looks up a file in the VFS by path and creates a user process from its image.
 * Parameters:
 *   const char* FilePath - VFS path to the executable file.
 * Returns:
 *   uint8_t - Process ID or 0xFF on failure.
 */
uint8_t LogicLayer::CreateUserProcessFromVFS(const char* FilePath)
{
    if (VFS == nullptr || FilePath == nullptr || Resource == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    Dentry* Entry = VFS->Lookup(FilePath);
    if (Entry == nullptr || Entry->inode == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    if (Entry->inode->NodeType != INODE_FILE)
    {
        return PROCESS_ID_INVALID;
    }

    if (Entry->inode->NodeData == nullptr || Entry->inode->NodeSize == 0)
    {
        return PROCESS_ID_INVALID;
    }

    uint64_t CodeSize = Entry->inode->NodeSize;
    uint64_t Pages    = (CodeSize + PAGE_SIZE - 1) / PAGE_SIZE;

    void* CopiedImage = Resource->GetPMM()->AllocatePagesFromDescriptor(Pages);
    if (CopiedImage == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    kmemset(CopiedImage, 0, Pages * PAGE_SIZE);
    memcpy(CopiedImage, Entry->inode->NodeData, CodeSize);

    uint8_t ProcessId = CreateUserProcess(reinterpret_cast<uint64_t>(CopiedImage), CodeSize);
    if (ProcessId == PROCESS_ID_INVALID)
    {
        Resource->GetPMM()->FreePagesFromDescriptor(CopiedImage, Pages);
        return ProcessId;
    }

    Process* CreatedProcess = PM->GetProcessById(ProcessId);
    if (CreatedProcess != nullptr)
    {
        CreatedProcess->CurrentFileSystemLocation = (Entry->parent != nullptr) ? Entry->parent : Entry;
    }

    return ProcessId;
}

/**
 * Function: LogicLayer::CreateUserProcess
 * Description: Creates a user process from raw binary or ELF image and schedules it.
 * Parameters:
 *   uint64_t CodeAddr - Physical address of program image.
 *   uint64_t CodeSize - Program image size in bytes.
 * Returns:
 *   uint8_t - Process ID or 0xFF on failure.
 */
uint8_t LogicLayer::CreateUserProcess(uint64_t CodeAddr, uint64_t CodeSize)
{
    if (CodeAddr == 0 || CodeSize == 0)
    {
        return PROCESS_ID_INVALID;
    }

    // Check if code is raw binary or ELF by looking for ELF magic number
    ELFHeader            Header         = {};
    VirtualAddressSpace* AddressSpace   = nullptr;
    bool                 IsELF          = false;
    uint64_t             UserEntryPoint = USER_PROCESS_VIRTUAL_BASE;

    if (ELF != nullptr)
    {
        Header = ELF->ParseELF(CodeAddr);
        IsELF  = ELF->ValidateELF(Header);
    }

    if (IsELF)
    {
        AddressSpace   = MapELF(CodeAddr, CodeSize, Header);
        UserEntryPoint = Header.Entry;
    }
    else
    {
        AddressSpace = MapRawBinary(CodeAddr, CodeSize);
    }

    if (AddressSpace == nullptr)
    {
        return PROCESS_ID_INVALID;
    }

    uint64_t StackTop = USER_PROCESS_VIRTUAL_STACK_TOP;

    CpuState State = {};
    State.rip      = UserEntryPoint;
    State.rflags   = INITIAL_PROCESS_RFLAGS;                                      // Bit1 always set, IF enabled
    State.rbp      = 0;                                                           // bottom of stack frame
    State.rsp      = (StackTop & ~STACK_ALIGNMENT_MASK) - STACK_RETURN_SLOT_SIZE; // SysV entry alignment without a real CALL

    State.cs   = USER_CS;
    State.ss   = USER_SS;
    uint8_t Id = PM->CreateUserProcess(reinterpret_cast<void*>(AddressSpace->GetStackVirtualAddressStart()), State, AddressSpace, IsELF ? FILE_TYPE_ELF : FILE_TYPE_RAW_BINARY);

    if (Id != PROCESS_ID_INVALID)
    {
        Sched->AddToReadyQueue(Id);
    }

    return Id;
}

/**
 * Function: LogicLayer::MapRawBinary
 * Description: Builds address space mappings for raw binary code, heap, and stack.
 * Parameters:
 *   uint64_t CodeAddr - Physical base address of raw code.
 *   uint64_t CodeSize - Raw code size in bytes.
 * Returns:
 *   VirtualAddressSpace* - Initialized address space or nullptr on failure.
 */
VirtualAddressSpace* LogicLayer::MapRawBinary(uint64_t CodeAddr, uint64_t CodeSize)
{
    // CodeAddr is a page-aligned physical address (from PMM) with CodeSize bytes copied in
    uint64_t CodePages = (CodeSize + PAGE_SIZE - 1) / PAGE_SIZE;

    void* ProcessStack = Resource->GetPMM()->AllocatePagesFromDescriptor(USER_PROCESS_STACK_SIZE / PAGE_SIZE);
    void* ProcessHeap  = Resource->GetPMM()->AllocatePagesFromDescriptor(USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
    if (ProcessStack == nullptr || ProcessHeap == nullptr)
    {
        if (ProcessStack != nullptr)
        {
            Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
        }

        if (ProcessHeap != nullptr)
        {
            Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
        }

        return nullptr;
    }

    uint64_t ProcessHeapVirtualAddrStart  = USER_PROCESS_VIRTUAL_BASE + (CodePages * PAGE_SIZE);
    uint64_t ProcessStackVirtualAddrStart = (USER_PROCESS_VIRTUAL_STACK_TOP + 1) - USER_PROCESS_STACK_SIZE;

    VirtualAddressSpace* AddressSpace = new VirtualAddressSpace(CodeAddr, CodeSize, USER_PROCESS_VIRTUAL_BASE, reinterpret_cast<uint64_t>(ProcessHeap), USER_PROCESS_HEAP_SIZE,
                                                                ProcessHeapVirtualAddrStart, reinterpret_cast<uint64_t>(ProcessStack), USER_PROCESS_STACK_SIZE, ProcessStackVirtualAddrStart);

    if (AddressSpace == nullptr)
    {
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
        return nullptr;
    }

    uint64_t ProcessPageMapL4TableAddr = reinterpret_cast<uint64_t>(Resource->GetVMM()->CopyPageMapL4Table());
    if (ProcessPageMapL4TableAddr == 0)
    {
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
        delete AddressSpace;
        return nullptr;
    }

    if (!AddressSpace->Init(ProcessPageMapL4TableAddr, *Resource->GetPMM()))
    {
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
        delete AddressSpace;
        return nullptr;
    }

    return AddressSpace;
}

/**
 * Function: LogicLayer::MapELF
 * Description: Builds address space mappings for ELF loadable segments plus heap and stack.
 * Parameters:
 *   uint64_t CodeAddr - Physical base address of ELF image.
 *   uint64_t CodeSize - ELF image size in bytes.
 *   const ELFHeader& Header - Parsed ELF header.
 * Returns:
 *   VirtualAddressSpace* - Initialized ELF address space or nullptr on failure.
 */
VirtualAddressSpace* LogicLayer::MapELF(uint64_t CodeAddr, uint64_t CodeSize, const ELFHeader& Header, const VirtualAddressSpaceELF* SourceRuntimeELFAddressSpace)
{
    if (ELF == nullptr || !ELF->ValidateProgramHeaderTable(Header, CodeSize))
    {
        return nullptr;
    }

    const ELFProgramHeader64* ProgramHeaders = ELF->GetProgramHeaderTable(CodeAddr, Header);
    if (ProgramHeaders == nullptr)
    {
        return nullptr;
    }

    uint64_t LowestVirtualAddress = 0;
    uint64_t HighestVirtualEnd    = 0;
    bool     HasLoadableSegment   = false;

    for (uint16_t ProgramHeaderIndex = 0; ProgramHeaderIndex < Header.ProgramHeaderEntryCount; ++ProgramHeaderIndex)
    {
        const ELFProgramHeader64& ProgramHeader = ProgramHeaders[ProgramHeaderIndex];
        if (!ELF->IsLoadableSegment(ProgramHeader))
        {
            continue;
        }

        if (!ELF->ValidateProgramSegment(ProgramHeader, CodeSize))
        {
            return nullptr;
        }

        uint64_t SegmentStart = ProgramHeader.VirtualAddress;
        uint64_t SegmentEnd   = AlignUpToPage(ProgramHeader.VirtualAddress + ProgramHeader.MemorySize);

        if (!HasLoadableSegment || SegmentStart < LowestVirtualAddress)
        {
            LowestVirtualAddress = SegmentStart;
        }

        if (!HasLoadableSegment || SegmentEnd > HighestVirtualEnd)
        {
            HighestVirtualEnd = SegmentEnd;
        }

        HasLoadableSegment = true;
    }

    if (!HasLoadableSegment)
    {
        return nullptr;
    }

    void* ProcessStack = Resource->GetPMM()->AllocatePagesFromDescriptor(USER_PROCESS_STACK_SIZE / PAGE_SIZE);
    void* ProcessHeap  = Resource->GetPMM()->AllocatePagesFromDescriptor(USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
    if (ProcessStack == nullptr || ProcessHeap == nullptr)
    {
        if (ProcessStack != nullptr)
        {
            Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
        }

        if (ProcessHeap != nullptr)
        {
            Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
        }

        return nullptr;
    }

    uint64_t ProcessHeapVirtualAddrStart  = AlignUpToPage(HighestVirtualEnd);
    uint64_t ProcessStackVirtualAddrStart = (USER_PROCESS_VIRTUAL_STACK_TOP + 1) - USER_PROCESS_STACK_SIZE;
    if (ProcessHeapVirtualAddrStart + USER_PROCESS_HEAP_SIZE > ProcessStackVirtualAddrStart)
    {
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
        return nullptr;
    }

    VirtualAddressSpaceELF* AddressSpace = new VirtualAddressSpaceELF(CodeAddr, CodeSize, LowestVirtualAddress, reinterpret_cast<uint64_t>(ProcessHeap), USER_PROCESS_HEAP_SIZE,
                                                                      ProcessHeapVirtualAddrStart, reinterpret_cast<uint64_t>(ProcessStack), USER_PROCESS_STACK_SIZE, ProcessStackVirtualAddrStart);

    if (AddressSpace == nullptr)
    {
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
        return nullptr;
    }

    for (uint16_t ProgramHeaderIndex = 0; ProgramHeaderIndex < Header.ProgramHeaderEntryCount; ++ProgramHeaderIndex)
    {
        const ELFProgramHeader64& ProgramHeader = ProgramHeaders[ProgramHeaderIndex];
        if (!ELF->IsLoadableSegment(ProgramHeader))
        {
            continue;
        }

        bool IsWritableSegment = ELF->IsWritableSegment(ProgramHeader);

        ELFMemoryRegion FileRegion = {};
        FileRegion.PhysicalAddress = CodeAddr + ProgramHeader.Offset;
        FileRegion.VirtualAddress  = ProgramHeader.VirtualAddress;
        FileRegion.Size            = ProgramHeader.FileSize;
        FileRegion.Writable        = IsWritableSegment;

        if (!AddressSpace->AddMemoryRegion(FileRegion))
        {
            Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
            Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
            delete AddressSpace;
            return nullptr;
        }

        if (ProgramHeader.MemorySize > ProgramHeader.FileSize)
        {
            uint64_t BssVirtualStart = ProgramHeader.VirtualAddress + ProgramHeader.FileSize;
            uint64_t BssRemaining    = ProgramHeader.MemorySize - ProgramHeader.FileSize;

            uint64_t BssTailBytes = 0;
            if ((BssVirtualStart & (PAGE_SIZE - 1)) != 0)
            {
                uint64_t TailCapacity = PAGE_SIZE - (BssVirtualStart & (PAGE_SIZE - 1));
                BssTailBytes          = (BssRemaining < TailCapacity) ? BssRemaining : TailCapacity;

                if (BssTailBytes != 0)
                {
                    uint64_t BssTailVirtual  = BssVirtualStart;
                    uint64_t BssTailPhysical = CodeAddr + ProgramHeader.Offset + ProgramHeader.FileSize;
                    kmemset(reinterpret_cast<void*>(BssTailPhysical), 0, static_cast<size_t>(BssTailBytes));

                    ELFMemoryRegion BssTailRegion = {};
                    BssTailRegion.PhysicalAddress = BssTailPhysical;
                    BssTailRegion.VirtualAddress  = BssTailVirtual;
                    BssTailRegion.Size            = BssTailBytes;
                    BssTailRegion.Writable        = IsWritableSegment;

                    if (!AddressSpace->AddMemoryRegion(BssTailRegion))
                    {
                        Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
                        Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
                        delete AddressSpace;
                        return nullptr;
                    }

                    BssVirtualStart += BssTailBytes;
                    BssRemaining -= BssTailBytes;
                }
            }

            if (BssRemaining != 0)
            {
                uint64_t BssPages    = (BssRemaining + PAGE_SIZE - 1) / PAGE_SIZE;
                void*    BssPhysical = Resource->GetPMM()->AllocatePagesFromDescriptor(BssPages);
                if (BssPhysical == nullptr)
                {
                    Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
                    Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
                    delete AddressSpace;
                    return nullptr;
                }

                kmemset(BssPhysical, 0, static_cast<size_t>(BssPages * PAGE_SIZE));

                ELFMemoryRegion BssRegion = {};
                BssRegion.PhysicalAddress = reinterpret_cast<uint64_t>(BssPhysical);
                BssRegion.VirtualAddress  = BssVirtualStart;
                BssRegion.Size            = BssRemaining;
                BssRegion.Writable        = IsWritableSegment;

                if (!AddressSpace->AddMemoryRegion(BssRegion))
                {
                    Resource->GetPMM()->FreePagesFromDescriptor(BssPhysical, BssPages);
                    Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
                    Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
                    delete AddressSpace;
                    return nullptr;
                }
            }
        }
    }

    uint64_t ProcessPageMapL4TableAddr = reinterpret_cast<uint64_t>(Resource->GetVMM()->CopyPageMapL4Table());
    if (ProcessPageMapL4TableAddr == 0)
    {
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
        delete AddressSpace;
        return nullptr;
    }

    if (!AddressSpace->Init(ProcessPageMapL4TableAddr, *Resource->GetPMM()))
    {
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessHeap, USER_PROCESS_HEAP_SIZE / PAGE_SIZE);
        Resource->GetPMM()->FreePagesFromDescriptor(ProcessStack, USER_PROCESS_STACK_SIZE / PAGE_SIZE);
        delete AddressSpace;
        return nullptr;
    }

    if (SourceRuntimeELFAddressSpace != nullptr)
    {
        const ELFMemoryRegion* SourceRegions = SourceRuntimeELFAddressSpace->GetMemoryRegions();
        size_t                 SourceCount   = SourceRuntimeELFAddressSpace->GetMemoryRegionCount();
        const ELFMemoryRegion* ChildRegions  = AddressSpace->GetMemoryRegions();
        size_t                 ChildCount    = AddressSpace->GetMemoryRegionCount();

        for (size_t SourceRegionIndex = 0; SourceRegionIndex < SourceCount; ++SourceRegionIndex)
        {
            const ELFMemoryRegion& SourceRegion = SourceRegions[SourceRegionIndex];
            if (!SourceRegion.Writable || SourceRegion.PhysicalAddress == 0 || SourceRegion.Size == 0)
            {
                continue;
            }

            const ELFMemoryRegion* MatchingChildRegion = nullptr;
            for (size_t ChildRegionIndex = 0; ChildRegionIndex < ChildCount; ++ChildRegionIndex)
            {
                const ELFMemoryRegion& Candidate = ChildRegions[ChildRegionIndex];
                if (Candidate.VirtualAddress != SourceRegion.VirtualAddress)
                {
                    continue;
                }

                MatchingChildRegion = &Candidate;
                break;
            }

            if (MatchingChildRegion == nullptr || MatchingChildRegion->PhysicalAddress == 0 || MatchingChildRegion->Size == 0)
            {
                CleanUpELF(AddressSpace);
                delete AddressSpace;
                return nullptr;
            }

            uint64_t BytesToCopy = (SourceRegion.Size < MatchingChildRegion->Size) ? SourceRegion.Size : MatchingChildRegion->Size;
            memcpy(reinterpret_cast<void*>(MatchingChildRegion->PhysicalAddress), reinterpret_cast<const void*>(SourceRegion.PhysicalAddress), static_cast<size_t>(BytesToCopy));
        }
    }

    return AddressSpace;
}

/**
 * Function: LogicLayer::CleanUpRawBinary
 * Description: Frees physical pages and page-map root associated with a raw-binary process address space.
 * Parameters:
 *   VirtualAddressSpace* AddressSpace - Address space to clean up.
 * Returns:
 *   void - No return value.
 */
void LogicLayer::CleanUpRawBinary(VirtualAddressSpace* AddressSpace)
{
    if (AddressSpace == nullptr || Resource == nullptr)
    {
        return;
    }

    PhysicalMemoryManager* PMM = Resource->GetPMM();
    if (PMM == nullptr)
    {
        return;
    }

    uint64_t CodePhysAddr     = AddressSpace->GetCodePhysicalAddress();
    uint64_t CodeSize         = AddressSpace->GetCodeSize();
    uint64_t HeapPhysAddr     = AddressSpace->GetHeapPhysicalAddress();
    uint64_t HeapSize         = AddressSpace->GetHeapSize();
    uint64_t StackPhysAddr    = AddressSpace->GetStackPhysicalAddress();
    uint64_t StackSize        = AddressSpace->GetStackSize();
    uint64_t ProcessPageMapL4 = AddressSpace->GetPageMapL4TableAddr();

    if (CodePhysAddr != 0 && CodeSize != 0)
    {
        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(CodePhysAddr), (CodeSize + PAGE_SIZE - 1) / PAGE_SIZE);
    }

    if (HeapPhysAddr != 0 && HeapSize != 0)
    {
        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(HeapPhysAddr), (HeapSize + PAGE_SIZE - 1) / PAGE_SIZE);
    }

    if (StackPhysAddr != 0 && StackSize != 0)
    {
        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(StackPhysAddr), (StackSize + PAGE_SIZE - 1) / PAGE_SIZE);
    }

    FreePageTableHierarchy(PMM, ProcessPageMapL4);
}

/**
 * Function: LogicLayer::CleanUpELF
 * Description: Frees physical ranges and page-map root associated with an ELF process address space.
 * Parameters:
 *   VirtualAddressSpace* AddressSpace - Address space to clean up.
 * Returns:
 *   void - No return value.
 */
void LogicLayer::CleanUpELF(VirtualAddressSpace* AddressSpace)
{
    if (AddressSpace == nullptr || Resource == nullptr)
    {
        return;
    }

    PhysicalMemoryManager* PMM = Resource->GetPMM();
    if (PMM == nullptr)
    {
        return;
    }

    VirtualAddressSpaceELF* ELFAddressSpace = static_cast<VirtualAddressSpaceELF*>(AddressSpace);
    const ELFMemoryRegion*  Regions         = ELFAddressSpace->GetMemoryRegions();
    size_t                  RegionCount     = ELFAddressSpace->GetMemoryRegionCount();

    uint64_t RangeStarts[16] = {};
    uint64_t RangeEnds[16]   = {};
    size_t   RangeCount      = 0;

    for (size_t RegionIndex = 0; RegionIndex < RegionCount; ++RegionIndex)
    {
        uint64_t RegionPhysicalAddress = Regions[RegionIndex].PhysicalAddress;
        uint64_t RegionSize            = Regions[RegionIndex].Size;
        if (RegionPhysicalAddress == 0 || RegionSize == 0)
        {
            continue;
        }

        uint64_t Start      = AlignDownToPage(RegionPhysicalAddress);
        uint64_t PageOffset = Regions[RegionIndex].VirtualAddress & (PAGE_SIZE - 1);
        uint64_t PageCount  = (PageOffset + RegionSize + PAGE_SIZE - 1) / PAGE_SIZE;
        if (PageCount == 0)
        {
            continue;
        }
        uint64_t End = Start + (PageCount * PAGE_SIZE);

        bool Merged = true;
        while (Merged)
        {
            Merged = false;
            for (size_t RangeIndex = 0; RangeIndex < RangeCount; ++RangeIndex)
            {
                if (End < RangeStarts[RangeIndex] || Start > RangeEnds[RangeIndex])
                {
                    continue;
                }

                if (RangeStarts[RangeIndex] < Start)
                {
                    Start = RangeStarts[RangeIndex];
                }
                if (RangeEnds[RangeIndex] > End)
                {
                    End = RangeEnds[RangeIndex];
                }

                RangeStarts[RangeIndex] = RangeStarts[RangeCount - 1];
                RangeEnds[RangeIndex]   = RangeEnds[RangeCount - 1];
                --RangeCount;
                Merged = true;
                break;
            }
        }

        if (RangeCount < 16)
        {
            RangeStarts[RangeCount] = Start;
            RangeEnds[RangeCount]   = End;
            ++RangeCount;
        }
    }

    for (size_t RangeIndex = 0; RangeIndex < RangeCount; ++RangeIndex)
    {
        uint64_t Start = RangeStarts[RangeIndex];
        uint64_t End   = RangeEnds[RangeIndex];
        if (End > Start)
        {
            PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(Start), (End - Start) / PAGE_SIZE);
        }
    }

    uint64_t HeapPhysAddr     = AddressSpace->GetHeapPhysicalAddress();
    uint64_t HeapSize         = AddressSpace->GetHeapSize();
    uint64_t StackPhysAddr    = AddressSpace->GetStackPhysicalAddress();
    uint64_t StackSize        = AddressSpace->GetStackSize();
    uint64_t ProcessPageMapL4 = AddressSpace->GetPageMapL4TableAddr();

    if (HeapPhysAddr != 0 && HeapSize != 0)
    {
        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(HeapPhysAddr), (HeapSize + PAGE_SIZE - 1) / PAGE_SIZE);
    }

    if (StackPhysAddr != 0 && StackSize != 0)
    {
        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(StackPhysAddr), (StackSize + PAGE_SIZE - 1) / PAGE_SIZE);
    }

    FreePageTableHierarchy(PMM, ProcessPageMapL4);
}

/**
 * Function: LogicLayer::KillProcess
 * Description: Terminates a process and releases its resources based on process type.
 * Parameters:
 *   uint8_t Id - Process ID to terminate.
 *   int32_t ExitStatus - Linux wait-style child exit status.
 * Returns:
 *   void - No return value.
 */
void LogicLayer::KillProcess(uint8_t Id, int32_t ExitStatus)
{
    Process* TargetProcess  = PM->GetProcessById(Id);
    Process* RunningProcess = PM->GetRunningProcess();
    uint8_t  ParentId       = PROCESS_ID_INVALID;

#ifdef DEBUG_BUILD
    TTY* DebugTerminal = (Resource == nullptr) ? nullptr : Resource->GetTTY();
    if (DebugTerminal != nullptr)
    {
        DebugTerminal->Serialprintf("kill_dbg: enter id=%u target=%p running=%p target_level=%d target_status=%d exit_status=%d\n", Id, TargetProcess, RunningProcess,
                                    (TargetProcess == nullptr) ? -1 : static_cast<int>(TargetProcess->Level), (TargetProcess == nullptr) ? -1 : static_cast<int>(TargetProcess->Status),
                                    static_cast<int>(ExitStatus));
    }
#endif

    if (TargetProcess != nullptr)
    {
        ParentId = TargetProcess->ParrentId;
    }

    bool SkipAddressSpaceCleanup = false;
    if (TargetProcess != nullptr && TargetProcess->IsVforkChild && TargetProcess->VforkParentId != PROCESS_ID_INVALID)
    {
        Process* ParentProcess = PM->GetProcessById(TargetProcess->VforkParentId);
        if (ParentProcess != nullptr && ParentProcess->AddressSpace == TargetProcess->AddressSpace)
        {
            SkipAddressSpaceCleanup = true;
        }
    }

#ifdef DEBUG_BUILD
    if (DebugTerminal != nullptr)
    {
        DebugTerminal->Serialprintf("kill_dbg: id=%u skip_addrspace_cleanup=%u\n", Id, SkipAddressSpaceCleanup ? 1U : 0U);
    }
#endif

    if (TargetProcess != nullptr && TargetProcess->Level == PROCESS_LEVEL_USER && TargetProcess->AddressSpace != nullptr && !SkipAddressSpaceCleanup)
    {
        if (RunningProcess != nullptr && RunningProcess->Id == Id && Resource != nullptr)
        {
            Resource->LoadKernelPageTable();
        }

        VirtualAddressSpace* AddressSpace = TargetProcess->AddressSpace;

        bool IsELFProcess = TargetProcess->FileType == FILE_TYPE_ELF;

        FreeAnonymousProcessMappings(Resource->GetPMM(), TargetProcess);

        PM->KillProcess(Id);

#ifdef DEBUG_BUILD
        if (DebugTerminal != nullptr)
        {
            Process* KilledProcess = PM->GetProcessById(Id);
            DebugTerminal->Serialprintf("kill_dbg: user_kill_done id=%u status=%d\n", Id, (KilledProcess == nullptr) ? -1 : static_cast<int>(KilledProcess->Status));
        }
#endif

        if (IsELFProcess)
        {
            CleanUpELF(AddressSpace);
        }
        else
        {
            CleanUpRawBinary(AddressSpace);
        }

        delete AddressSpace;
        TargetProcess->AddressSpace = nullptr;
    }
    else
    {
        void* StackToFree = PM->KillProcess(Id);
#ifdef DEBUG_BUILD
        if (DebugTerminal != nullptr)
        {
            Process* KilledProcess = PM->GetProcessById(Id);
            DebugTerminal->Serialprintf("kill_dbg: generic_kill_done id=%u status=%d stack=%p\n", Id, (KilledProcess == nullptr) ? -1 : static_cast<int>(KilledProcess->Status), StackToFree);
        }
#endif
        if (StackToFree != nullptr && TargetProcess != nullptr && TargetProcess->Level == PROCESS_LEVEL_KERNEL)
        {
            Resource->kfree(StackToFree);
        }
    }

    Sched->RemoveFromReadyQueue(Id);

    if (Sync != nullptr)
    {
        Sync->RemoveFromSleepQueue(Id);
        Sync->RemoveFromTTYInputWaitQueue(Id);
    }

#ifdef DEBUG_BUILD
    if (DebugTerminal != nullptr)
    {
        Process* CurrentAfterKill = PM->GetCurrentProcess();
        DebugTerminal->Serialprintf("kill_dbg: exit id=%u current_after=%d\n", Id, (CurrentAfterKill == nullptr) ? -1 : static_cast<int>(CurrentAfterKill->Id));
    }
#endif

    Process* ParentProcess = PM->GetProcessById(ParentId);
    if (ParentProcess != nullptr && ParentProcess->Status != PROCESS_TERMINATED)
    {
        ParentProcess->HasPendingChildExit = true;
        ParentProcess->PendingChildId      = Id;
        ParentProcess->PendingChildStatus  = ExitStatus;

        if (ParentProcess->WaitingForChild)
        {
            ParentProcess->WaitingForChild = false;
            UnblockProcess(ParentProcess->Id);
        }
    }
}

void LogicLayer::AddProcessToReadyQueue(uint8_t Id)
{
    if (PM == nullptr || Sched == nullptr)
    {
        return;
    }

    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status == PROCESS_TERMINATED)
    {
        return;
    }

    if (TargetProcess->Status == PROCESS_BLOCKED)
    {
        TargetProcess->Status = PROCESS_READY;
    }

    Sched->AddToReadyQueue(Id);
}

bool LogicLayer::CopyFromUserToKernel(const void* UserSource, void* KernelDestination, uint64_t Count)
{
    if ((UserSource == nullptr && Count != 0) || (KernelDestination == nullptr && Count != 0))
    {
        return false;
    }

    if (Count == 0)
    {
        return true;
    }

    if (PM == nullptr)
    {
        return false;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (!IsUserAddressRangeAccessible(CurrentProcess, reinterpret_cast<uint64_t>(UserSource), Count))
    {
        return false;
    }

    if (Resource == nullptr || CurrentProcess->AddressSpace == nullptr)
    {
        return false;
    }

    uint64_t UserPageTable = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
    if (UserPageTable == 0)
    {
        return false;
    }

    uint64_t PreviousPageTable = Resource->ReadCurrentPageTable();
    if (PreviousPageTable == 0)
    {
        return false;
    }

    bool NeedsPageTableSwitch = (PreviousPageTable != UserPageTable);
    if (NeedsPageTableSwitch)
    {
        Resource->LoadPageTable(UserPageTable);
    }

    memcpy(KernelDestination, UserSource, static_cast<size_t>(Count));

    if (NeedsPageTableSwitch)
    {
        Resource->LoadPageTable(PreviousPageTable);
    }

    return true;
}

bool LogicLayer::CopyFromKernelToUser(const void* KernelSource, void* UserDestination, uint64_t Count)
{
    if ((KernelSource == nullptr && Count != 0) || (UserDestination == nullptr && Count != 0))
    {
        return false;
    }

    if (Count == 0)
    {
        return true;
    }

    if (PM == nullptr)
    {
        return false;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (!IsUserAddressRangeAccessible(CurrentProcess, reinterpret_cast<uint64_t>(UserDestination), Count))
    {
        return false;
    }

    if (Resource == nullptr || CurrentProcess->AddressSpace == nullptr)
    {
        return false;
    }

    uint64_t UserPageTable = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
    if (UserPageTable == 0)
    {
        return false;
    }

    uint64_t PreviousPageTable = Resource->ReadCurrentPageTable();
    if (PreviousPageTable == 0)
    {
        return false;
    }

    bool NeedsPageTableSwitch = (PreviousPageTable != UserPageTable);
    if (NeedsPageTableSwitch)
    {
        Resource->LoadPageTable(UserPageTable);
    }

    memcpy(UserDestination, KernelSource, static_cast<size_t>(Count));

    if (NeedsPageTableSwitch)
    {
        Resource->LoadPageTable(PreviousPageTable);
    }

    return true;
}

/**
 * Function: LogicLayer::RunProcess
 * Description: Switches execution from current process to target process.
 * Parameters:
 *   uint8_t Id - Target process ID to run.
 * Returns:
 *   bool - True if switch request was valid, false otherwise.
 */
bool LogicLayer::RunProcess(uint8_t Id)
{
    if (PM == nullptr || Resource == nullptr)
    {
        return false;
    }

    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status == PROCESS_TERMINATED)
    {
        return false;
    }

    Process* CurrentProcess              = PM->GetCurrentProcess();
    bool     ResumeTargetInKernelContext = (TargetProcess->Level == PROCESS_LEVEL_USER && TargetProcess->WaitingForSystemCallReturn && TargetProcess->HasSavedSystemCallFrame
                                        && TargetProcess->State.cs == KERNEL_CS && TargetProcess->State.ss == KERNEL_SS);

    if (CurrentProcess == nullptr)
    {
        TargetProcess->Status = PROCESS_RUNNING;
        PM->UpdateCurrentProcessId(TargetProcess->Id);

        CpuState BootstrapState = {};
        BootstrapState.cs       = KERNEL_CS;
        BootstrapState.ss       = KERNEL_SS;

        if (TargetProcess->Level == PROCESS_LEVEL_USER && !ResumeTargetInKernelContext)
        {
            SetKernelSystemCallStackTop(TargetProcess->KernelSystemCallStackTop);
            SetUserFSBase(TargetProcess->UserFSBase);
            Resource->TaskSwitchUser(&BootstrapState, TargetProcess->State, TargetProcess->AddressSpace);
        }
        else if (ResumeTargetInKernelContext)
        {
            if (TargetProcess->AddressSpace == nullptr)
            {
                return false;
            }

            uint64_t TargetPageTable = TargetProcess->AddressSpace->GetPageMapL4TableAddr();
            if (TargetPageTable == 0)
            {
                return false;
            }

            SetKernelSystemCallStackTop(TargetProcess->KernelSystemCallStackTop);
            SetUserFSBase(TargetProcess->UserFSBase);
            Resource->LoadPageTable(TargetPageTable);
            Resource->TaskSwitchKernelCurrentAddressSpace(&BootstrapState, TargetProcess->State);
        }
        else
        {
            Resource->TaskSwitchKernel(&BootstrapState, TargetProcess->State);
        }

        return true;
    }

    if (CurrentProcess == TargetProcess)
    {
        return true;
    }

    if (CurrentProcess->Status == PROCESS_RUNNING)
    {
        CurrentProcess->Status = PROCESS_READY;
    }
    TargetProcess->Status = PROCESS_RUNNING;
    PM->UpdateCurrentProcessId(TargetProcess->Id);
    if (TargetProcess->Level == PROCESS_LEVEL_USER && !ResumeTargetInKernelContext)
    {
        SetKernelSystemCallStackTop(TargetProcess->KernelSystemCallStackTop);
        SetUserFSBase(TargetProcess->UserFSBase);
        Resource->TaskSwitchUser(&CurrentProcess->State, TargetProcess->State, TargetProcess->AddressSpace);
    }
    else if (ResumeTargetInKernelContext)
    {
        if (TargetProcess->AddressSpace == nullptr)
        {
            return false;
        }

        uint64_t TargetPageTable = TargetProcess->AddressSpace->GetPageMapL4TableAddr();
        if (TargetPageTable == 0)
        {
            return false;
        }

        SetKernelSystemCallStackTop(TargetProcess->KernelSystemCallStackTop);
        SetUserFSBase(TargetProcess->UserFSBase);
        Resource->LoadPageTable(TargetPageTable);
        Resource->TaskSwitchKernelCurrentAddressSpace(&CurrentProcess->State, TargetProcess->State);
    }
    else
    {
        Resource->TaskSwitchKernel(&CurrentProcess->State, TargetProcess->State);
    }

    if (TargetProcess->Status == PROCESS_RUNNING)
    {
        TargetProcess->Status = PROCESS_READY;
    }
    CurrentProcess->Status = PROCESS_RUNNING;

    return true;
}

/**
 * Function: LogicLayer::SleepProcess
 * Description: Blocks a running process for a specified tick duration and triggers reschedule.
 * Parameters:
 *   uint8_t Id - Process ID to sleep.
 *   uint64_t WaitTicks - Number of ticks to sleep.
 * Returns:
 *   void - No return value.
 */
void LogicLayer::SleepProcess(uint8_t Id, uint64_t WaitTicks)
{
    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status != PROCESS_RUNNING)
    {
        return;
    }

    TargetProcess->Status = PROCESS_BLOCKED;
    Sync->AddToSleepQueue(Id, WaitTicks);
    Sched->RemoveFromReadyQueue(Id);

    Ticks = 0; // Reset ticks to ensure the sleeping process gets the correct sleep duration
    Schedule();
}

/**
 * Function: LogicLayer::WakeProcess
 * Description: Wakes a blocked process and requeues it as ready.
 * Parameters:
 *   uint8_t Id - Process ID to wake.
 * Returns:
 *   void - No return value.
 */
void LogicLayer::WakeProcess(uint8_t Id)
{
    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status != PROCESS_BLOCKED)
    {
        return;
    }

    TargetProcess->Status = PROCESS_READY;
    Sync->RemoveFromSleepQueue(Id);
    Sched->AddToReadyQueue(Id);
}

void LogicLayer::BlockProcessForTTYInput(uint8_t Id)
{
    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status != PROCESS_RUNNING)
    {
        return;
    }

    if (TargetProcess->WaitingForSystemCallReturn && TargetProcess->HasSavedSystemCallFrame)
    {
        TargetProcess->State.cs = KERNEL_CS;
        TargetProcess->State.ss = KERNEL_SS;
    }

    TargetProcess->Status = PROCESS_BLOCKED;
    Sync->AddToTTYInputWaitQueue(Id);
    Sched->RemoveFromReadyQueue(Id);

    Ticks = 0;
    Schedule();
}

void LogicLayer::NotifyTTYInputAvailable()
{
    if (Sync == nullptr)
    {
        return;
    }

    uint8_t TTYWaiterId = Sync->GetNextTTYInputWaiter();
    if (TTYWaiterId == PROCESS_ID_INVALID)
    {
        return;
    }

    Sync->RemoveFromTTYInputWaitQueue(TTYWaiterId);
    UnblockProcess(TTYWaiterId);
}

/**
 * Function: LogicLayer::BlockProcess
 * Description: Blocks a running process and triggers scheduler.
 * Parameters:
 *   uint8_t Id - Process ID to block.
 * Returns:
 *   void - No return value.
 */
void LogicLayer::BlockProcess(uint8_t Id)
{
    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status != PROCESS_RUNNING)
    {
        return;
    }

    if (TargetProcess->WaitingForSystemCallReturn && TargetProcess->HasSavedSystemCallFrame)
    {
        TargetProcess->State.cs = KERNEL_CS;
        TargetProcess->State.ss = KERNEL_SS;
    }

    TargetProcess->Status = PROCESS_BLOCKED;
    Sched->RemoveFromReadyQueue(Id);

    Ticks = 0; // Reset ticks
    Schedule();
}

/**
 * Function: LogicLayer::UnblockProcess
 * Description: Moves a blocked process back to ready state.
 * Parameters:
 *   uint8_t Id - Process ID to unblock.
 * Returns:
 *   void - No return value.
 */
void LogicLayer::UnblockProcess(uint8_t Id)
{
    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status != PROCESS_BLOCKED)
    {
        return;
    }

    TargetProcess->Status = PROCESS_READY;
    Sched->AddToReadyQueue(Id);
}

/**
 * Function: LogicLayer::CaptureCurrentInterruptState
 * Description: Captures user-mode CPU register state from interrupt frame into running process state.
 * Parameters:
 *   const Registers* Regs - Interrupt register frame.
 * Returns:
 *   void - No return value.
 */
void LogicLayer::CaptureCurrentInterruptState(const Registers* Regs)
{
    if (Regs == nullptr || PM == nullptr)
    {
        return;
    }

    if ((Regs->cs & USER_MODE_FLAG_MASK) != USER_MODE_FLAG_MASK)
    {
        return;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER)
    {
        return;
    }

    CurrentProcess->State.rax    = Regs->rax;
    CurrentProcess->State.rcx    = Regs->rcx;
    CurrentProcess->State.rdx    = Regs->rdx;
    CurrentProcess->State.rbx    = Regs->rbx;
    CurrentProcess->State.rbp    = Regs->rbp;
    CurrentProcess->State.rsi    = Regs->rsi;
    CurrentProcess->State.rdi    = Regs->rdi;
    CurrentProcess->State.r8     = Regs->r8;
    CurrentProcess->State.r9     = Regs->r9;
    CurrentProcess->State.r10    = Regs->r10;
    CurrentProcess->State.r11    = Regs->r11;
    CurrentProcess->State.r12    = Regs->r12;
    CurrentProcess->State.r13    = Regs->r13;
    CurrentProcess->State.r14    = Regs->r14;
    CurrentProcess->State.r15    = Regs->r15;
    CurrentProcess->State.rip    = Regs->rip;
    CurrentProcess->State.rflags = Regs->rflags;
    CurrentProcess->State.rsp    = Regs->rsp;
    CurrentProcess->State.cs     = Regs->cs;
    CurrentProcess->State.ss     = Regs->ss;
}

/**
 * Function: LogicLayer::Tick
 * Description: Advances synchronization timers and wakes processes whose sleep expired.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void LogicLayer::Tick()
{
    if (Sync != nullptr)
    {
        Sync->Tick();
        uint8_t IdToWake = Sync->GetNextProcessToWake();
        if (IdToWake != PROCESS_ID_INVALID)
        {
            WakeProcess(IdToWake);
        }
    }
}

/**
 * Function: LogicLayer::Schedule
 * Description: Selects next ready process and requests context switch.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void LogicLayer::Schedule()
{
    if (Sched == nullptr || PM == nullptr)
    {
        return;
    }

    uint8_t NextProcessId = Sched->SelectNextProcess();

    // Resource->GetConsole()->printf_("Scheduling: Next process ID = %u\n", NextProcessId);

    if (NextProcessId == PROCESS_ID_INVALID)
    {
        return;
    }

    RunProcess(NextProcessId);
}

/**
 * Function: LogicLayer::isScheduling
 * Description: Returns whether scheduler-driven switching is enabled.
 * Parameters:
 *   None
 * Returns:
 *   bool - True if scheduling is enabled.
 */
bool LogicLayer::isScheduling()
{
    return IsScheduling;
}

/**
 * Function: LogicLayer::EnableScheduling
 * Description: Enables scheduler-driven context switching.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void LogicLayer::EnableScheduling()
{
    IsScheduling = true;
}

/**
 * Function: LogicLayer::DisableScheduling
 * Description: Disables scheduler-driven context switching.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void LogicLayer::DisableScheduling()
{
    IsScheduling = false;
}