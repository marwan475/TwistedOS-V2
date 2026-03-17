/**
 * File: LogicLayer.cpp
 * Author: Marwan Mostafa
 * Description: Logic layer orchestration implementation.
 */

#include "LogicLayer.hpp"

#include "Layers/Resource/ResourceLayer.hpp"

#include <CommonUtils.hpp>

namespace
{
constexpr uint64_t INITIAL_PROCESS_RFLAGS = 0x202;
constexpr uint64_t USER_MODE_FLAG_MASK    = 0x3;
constexpr uint64_t STACK_ALIGNMENT_MASK   = 0xFULL;
constexpr uint64_t STACK_RETURN_SLOT_SIZE = 8;

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
        __asm__ __volatile__("hlt");
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
    Resource->GetConsole()->printf_("Process Manager Initialized\n");
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
    Resource->GetConsole()->printf_("Scheduler Initialized\n");
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
    Resource->GetConsole()->printf_("Synchronization Manager Initialized\n");
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
    Resource->GetConsole()->printf_("ELF Manager Initialized\n");
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
    Resource->GetConsole()->printf_("Virtual File System Initialized\n");
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
    void*    ProcessStack = Resource->kmalloc(KERNEL_PROCESS_STACK_SIZE);
    uint64_t StackTop     = reinterpret_cast<uint64_t>(ProcessStack) + KERNEL_PROCESS_STACK_SIZE;

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

    return Id;
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
        return nullptr;
    }

    uint64_t ProcessHeapVirtualAddrStart  = USER_PROCESS_VIRTUAL_BASE + (CodePages * PAGE_SIZE);
    uint64_t ProcessStackVirtualAddrStart = (USER_PROCESS_VIRTUAL_STACK_TOP + 1) - USER_PROCESS_STACK_SIZE;

    VirtualAddressSpace* AddressSpace = new VirtualAddressSpace(CodeAddr, CodeSize, USER_PROCESS_VIRTUAL_BASE, reinterpret_cast<uint64_t>(ProcessHeap), USER_PROCESS_HEAP_SIZE,
                                                                ProcessHeapVirtualAddrStart, reinterpret_cast<uint64_t>(ProcessStack), USER_PROCESS_STACK_SIZE, ProcessStackVirtualAddrStart);

    if (AddressSpace == nullptr)
    {
        return nullptr;
    }

    uint64_t ProcessPageMapL4TableAddr = reinterpret_cast<uint64_t>(Resource->GetVMM()->CopyPageMapL4Table());
    if (ProcessPageMapL4TableAddr == 0)
    {
        delete AddressSpace;
        return nullptr;
    }

    if (!AddressSpace->Init(ProcessPageMapL4TableAddr, *Resource->GetPMM()))
    {
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
VirtualAddressSpace* LogicLayer::MapELF(uint64_t CodeAddr, uint64_t CodeSize, const ELFHeader& Header)
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
        return nullptr;
    }

    uint64_t ProcessHeapVirtualAddrStart  = AlignUpToPage(HighestVirtualEnd);
    uint64_t ProcessStackVirtualAddrStart = (USER_PROCESS_VIRTUAL_STACK_TOP + 1) - USER_PROCESS_STACK_SIZE;
    if (ProcessHeapVirtualAddrStart + USER_PROCESS_HEAP_SIZE > ProcessStackVirtualAddrStart)
    {
        return nullptr;
    }

    VirtualAddressSpaceELF* AddressSpace = new VirtualAddressSpaceELF(CodeAddr, CodeSize, LowestVirtualAddress, reinterpret_cast<uint64_t>(ProcessHeap), USER_PROCESS_HEAP_SIZE,
                                                                      ProcessHeapVirtualAddrStart, reinterpret_cast<uint64_t>(ProcessStack), USER_PROCESS_STACK_SIZE, ProcessStackVirtualAddrStart);

    if (AddressSpace == nullptr)
    {
        return nullptr;
    }

    for (uint16_t ProgramHeaderIndex = 0; ProgramHeaderIndex < Header.ProgramHeaderEntryCount; ++ProgramHeaderIndex)
    {
        const ELFProgramHeader64& ProgramHeader = ProgramHeaders[ProgramHeaderIndex];
        if (!ELF->IsLoadableSegment(ProgramHeader))
        {
            continue;
        }

        ELFMemoryRegion Region = {};
        Region.PhysicalAddress = CodeAddr + ProgramHeader.Offset;
        Region.VirtualAddress  = ProgramHeader.VirtualAddress;
        Region.Size            = ProgramHeader.MemorySize;
        Region.Writable        = ELF->IsWritableSegment(ProgramHeader);

        if (!AddressSpace->AddMemoryRegion(Region))
        {
            delete AddressSpace;
            return nullptr;
        }
    }

    uint64_t ProcessPageMapL4TableAddr = reinterpret_cast<uint64_t>(Resource->GetVMM()->CopyPageMapL4Table());
    if (ProcessPageMapL4TableAddr == 0)
    {
        delete AddressSpace;
        return nullptr;
    }

    if (!AddressSpace->Init(ProcessPageMapL4TableAddr, *Resource->GetPMM()))
    {
        delete AddressSpace;
        return nullptr;
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

    if (ProcessPageMapL4 != 0)
    {
        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(ProcessPageMapL4), 1);
    }
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

    uint64_t CodePhysAddr     = AddressSpace->GetCodePhysicalAddress();
    uint64_t CodeSize         = AddressSpace->GetCodeSize();
    uint64_t CodePages        = (CodeSize + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t HeapPhysAddr     = AddressSpace->GetHeapPhysicalAddress();
    uint64_t HeapSize         = AddressSpace->GetHeapSize();
    uint64_t StackPhysAddr    = AddressSpace->GetStackPhysicalAddress();
    uint64_t StackSize        = AddressSpace->GetStackSize();
    uint64_t ProcessPageMapL4 = AddressSpace->GetPageMapL4TableAddr();

    if (CodePhysAddr != 0 && CodeSize != 0)
    {
        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(CodePhysAddr), CodePages);
    }

    if (HeapPhysAddr != 0 && HeapSize != 0)
    {
        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(HeapPhysAddr), (HeapSize + PAGE_SIZE - 1) / PAGE_SIZE);
    }

    if (StackPhysAddr != 0 && StackSize != 0)
    {
        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(StackPhysAddr), (StackSize + PAGE_SIZE - 1) / PAGE_SIZE);
    }

    if (ProcessPageMapL4 != 0)
    {
        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(ProcessPageMapL4), 1);
    }
}

/**
 * Function: LogicLayer::KillProcess
 * Description: Terminates a process and releases its resources based on process type.
 * Parameters:
 *   uint8_t Id - Process ID to terminate.
 * Returns:
 *   void - No return value.
 */
void LogicLayer::KillProcess(uint8_t Id)
{
    Process* TargetProcess = PM->GetProcessById(Id);

    if (TargetProcess != nullptr && TargetProcess->Level == PROCESS_LEVEL_USER && TargetProcess->AddressSpace != nullptr)
    {
        VirtualAddressSpace* AddressSpace = TargetProcess->AddressSpace;

        bool IsELFProcess = TargetProcess->FileType == FILE_TYPE_ELF;

        PM->KillProcess(Id);

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
        if (StackToFree != nullptr)
        {
            Resource->kfree(StackToFree);
        }
    }

    Sched->RemoveFromReadyQueue(Id);
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

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return false;
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
    if (TargetProcess->Level == PROCESS_LEVEL_USER)
    {
        Resource->TaskSwitchUser(&CurrentProcess->State, TargetProcess->State, TargetProcess->AddressSpace);
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