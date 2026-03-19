/**
 * File: TranslationLayer.cpp
 * Author: Marwan Mostafa
 * Description: Translation layer implementation between system layers.
 */

#include "TranslationLayer.hpp"

#include "Layers/Logic/LogicLayer.hpp"

#include <Arch/x86.hpp>
#include <CommonUtils.hpp>
#include <Layers/Dispatcher.hpp>
#include <Layers/Resource/ResourceLayer.hpp>
#include <Layers/Resource/VirtualAddressSpace.hpp>
#include <Memory/VirtualMemoryManager.hpp>

namespace
{
constexpr int64_t LINUX_ERR_EFAULT = -14;
constexpr int64_t LINUX_ERR_ENOENT = -2;
constexpr int64_t LINUX_ERR_ENOMEM = -12;
constexpr int64_t LINUX_ERR_EAGAIN = -11;
constexpr int64_t LINUX_ERR_EMFILE = -24;
constexpr int64_t LINUX_ERR_EINVAL = -22;
constexpr int64_t LINUX_ERR_EBADF  = -9;
constexpr int64_t LINUX_ERR_ENOSYS = -38;
constexpr int64_t LINUX_ERR_ECHILD = -10;
constexpr int64_t LINUX_ERR_ENODEV = -19;
constexpr int64_t LINUX_ERR_EPERM  = -1;

constexpr uint64_t SYSCALL_COPY_CHUNK_SIZE = 4096;
constexpr uint64_t SYSCALL_PATH_MAX        = 4096;
constexpr uint64_t SYSCALL_EXEC_MAX_VECTOR = 128;

constexpr int64_t LINUX_MAP_SHARED    = 0x01;
constexpr int64_t LINUX_MAP_PRIVATE   = 0x02;
constexpr int64_t LINUX_MAP_FIXED     = 0x10;
constexpr int64_t LINUX_MAP_ANONYMOUS = 0x20;

constexpr int64_t LINUX_PROT_READ  = 0x1;
constexpr int64_t LINUX_PROT_WRITE = 0x2;
constexpr int64_t LINUX_PROT_EXEC  = 0x4;
constexpr int64_t LINUX_PROT_NONE  = 0x0;

constexpr uint64_t MMAP_DEFAULT_BASE = 0x0000000001000000;

constexpr uint64_t LINUX_O_ACCMODE = 0x3;

constexpr uint64_t LINUX_ARCH_SET_FS = 0x1002;

bool IsCanonicalX86_64Address(uint64_t Address)
{
    constexpr uint64_t LOWER_CANONICAL_MAX = 0x00007FFFFFFFFFFFULL;
    constexpr uint64_t UPPER_CANONICAL_MIN = 0xFFFF800000000000ULL;
    return (Address <= LOWER_CANONICAL_MAX) || (Address >= UPPER_CANONICAL_MIN);
}

FileFlags DecodeAccessFlags(uint64_t Flags)
{
    uint64_t AccessMode = Flags & LINUX_O_ACCMODE;

    if (AccessMode == 1)
    {
        return WRITE;
    }

    if (AccessMode == 2)
    {
        return READ_WRITE;
    }

    return READ;
}

bool CopyUserCString(LogicLayer* Logic, const char* UserString, char* KernelBuffer, uint64_t KernelBufferSize)
{
    if (Logic == nullptr || UserString == nullptr || KernelBuffer == nullptr || KernelBufferSize == 0)
    {
        return false;
    }

    for (uint64_t Index = 0; Index < KernelBufferSize; ++Index)
    {
        char        Character            = 0;
        const void* UserCharacterAddress = reinterpret_cast<const void*>(reinterpret_cast<uint64_t>(UserString) + Index);
        if (!Logic->CopyFromUserToKernel(UserCharacterAddress, &Character, sizeof(Character)))
        {
            return false;
        }

        KernelBuffer[Index] = Character;
        if (Character == '\0')
        {
            return true;
        }
    }

    KernelBuffer[KernelBufferSize - 1] = '\0';
    return false;
}

uint64_t CStrLength(const char* String)
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

void FreeKernelStringVector(LogicLayer* Logic, char** KernelVector, uint64_t Count)
{
    if (Logic == nullptr || KernelVector == nullptr)
    {
        return;
    }

    for (uint64_t Index = 0; Index < Count; ++Index)
    {
        if (KernelVector[Index] != nullptr)
        {
            Logic->kfree(KernelVector[Index]);
        }
    }

    Logic->kfree(KernelVector);
}

bool CopyUserStringVector(LogicLayer* Logic, const char* const* UserVector, char*** KernelVectorOut, uint64_t* CountOut)
{
    if (Logic == nullptr || KernelVectorOut == nullptr || CountOut == nullptr)
    {
        return false;
    }

    *KernelVectorOut = nullptr;
    *CountOut        = 0;

    if (UserVector == nullptr)
    {
        return true;
    }

    char** KernelVector = reinterpret_cast<char**>(Logic->kmalloc(sizeof(char*) * (SYSCALL_EXEC_MAX_VECTOR + 1)));
    if (KernelVector == nullptr)
    {
        return false;
    }

    for (uint64_t Index = 0; Index < (SYSCALL_EXEC_MAX_VECTOR + 1); ++Index)
    {
        KernelVector[Index] = nullptr;
    }

    char TempBuffer[SYSCALL_PATH_MAX];

    for (uint64_t Index = 0; Index < SYSCALL_EXEC_MAX_VECTOR; ++Index)
    {
        const char* UserEntry = nullptr;
        const void* UserEntryAddress = reinterpret_cast<const void*>(reinterpret_cast<uint64_t>(UserVector) + (Index * sizeof(const char*)));
        if (!Logic->CopyFromUserToKernel(UserEntryAddress, &UserEntry, sizeof(UserEntry)))
        {
            FreeKernelStringVector(Logic, KernelVector, Index);
            return false;
        }

        if (UserEntry == nullptr)
        {
            *KernelVectorOut = KernelVector;
            *CountOut        = Index;
            return true;
        }

        if (!CopyUserCString(Logic, UserEntry, TempBuffer, sizeof(TempBuffer)))
        {
            FreeKernelStringVector(Logic, KernelVector, Index);
            return false;
        }

        uint64_t EntryLength = CStrLength(TempBuffer) + 1;
        KernelVector[Index]  = reinterpret_cast<char*>(Logic->kmalloc(EntryLength));
        if (KernelVector[Index] == nullptr)
        {
            FreeKernelStringVector(Logic, KernelVector, Index);
            return false;
        }

        for (uint64_t CharIndex = 0; CharIndex < EntryLength; ++CharIndex)
        {
            KernelVector[Index][CharIndex] = TempBuffer[CharIndex];
        }
    }

    FreeKernelStringVector(Logic, KernelVector, SYSCALL_EXEC_MAX_VECTOR);
    return false;
}

bool ProcessHasLiveChild(ProcessManager* PM, uint8_t ParentId)
{
    if (PM == nullptr)
    {
        return false;
    }

    for (size_t Index = 0; Index < PM->GetMaxProcesses(); ++Index)
    {
        Process* CandidateProcess = PM->GetProcessById(static_cast<uint8_t>(Index));
        if (CandidateProcess == nullptr)
        {
            continue;
        }

        if (CandidateProcess->ParrentId == ParentId && CandidateProcess->Status != PROCESS_TERMINATED)
        {
            return true;
        }
    }

    return false;
}

uint64_t AlignDownToPageBoundary(uint64_t Address)
{
    return Address & PHYS_PAGE_ADDR_MASK;
}

uint64_t AlignUpToPageBoundary(uint64_t Value)
{
    if (Value == 0)
    {
        return 0;
    }

    if (Value > (UINT64_MAX - (PAGE_SIZE - 1)))
    {
        return 0;
    }

    return (Value + PAGE_SIZE - 1) & PHYS_PAGE_ADDR_MASK;
}

bool RangesOverlap(uint64_t StartA, uint64_t LengthA, uint64_t StartB, uint64_t LengthB)
{
    if (LengthA == 0 || LengthB == 0)
    {
        return false;
    }

    uint64_t EndA = StartA + LengthA;
    uint64_t EndB = StartB + LengthB;
    if (EndA < StartA || EndB < StartB)
    {
        return true;
    }

    return (StartA < EndB) && (StartB < EndA);
}

bool MappingOverlapsProcessLayout(const Process* CurrentProcess, uint64_t MappingStart, uint64_t MappingLength)
{
    if (CurrentProcess == nullptr || CurrentProcess->AddressSpace == nullptr)
    {
        return true;
    }

    const VirtualAddressSpace* AddressSpace = CurrentProcess->AddressSpace;

    if (CurrentProcess->FileType == FILE_TYPE_ELF)
    {
        const VirtualAddressSpaceELF* ELFAddressSpace = static_cast<const VirtualAddressSpaceELF*>(AddressSpace);
        const ELFMemoryRegion*        Regions         = ELFAddressSpace->GetMemoryRegions();
        size_t                        RegionCount     = ELFAddressSpace->GetMemoryRegionCount();

        for (size_t RegionIndex = 0; RegionIndex < RegionCount; ++RegionIndex)
        {
            if (RangesOverlap(MappingStart, MappingLength, Regions[RegionIndex].VirtualAddress, Regions[RegionIndex].Size))
            {
                return true;
            }
        }
    }

    if (RangesOverlap(MappingStart, MappingLength, AddressSpace->GetCodeVirtualAddressStart(), AddressSpace->GetCodeSize())
        || RangesOverlap(MappingStart, MappingLength, AddressSpace->GetHeapVirtualAddressStart(), AddressSpace->GetHeapSize())
        || RangesOverlap(MappingStart, MappingLength, AddressSpace->GetStackVirtualAddressStart(), AddressSpace->GetStackSize()))
    {
        return true;
    }

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        const ProcessMemoryMapping& ExistingMapping = CurrentProcess->MemoryMappings[MappingIndex];
        if (!ExistingMapping.InUse)
        {
            continue;
        }

        if (RangesOverlap(MappingStart, MappingLength, ExistingMapping.VirtualAddressStart, ExistingMapping.Length))
        {
            return true;
        }
    }

    return false;
}

int64_t FindAvailableMappingSlot(const Process* CurrentProcess)
{
    if (CurrentProcess == nullptr)
    {
        return -1;
    }

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        if (!CurrentProcess->MemoryMappings[MappingIndex].InUse)
        {
            return static_cast<int64_t>(MappingIndex);
        }
    }

    return -1;
}

bool RegisterProcessMapping(Process* CurrentProcess, uint64_t MappingStart, uint64_t MappingLength, uint64_t PhysicalStart, bool IsAnonymous)
{
    int64_t MappingSlot = FindAvailableMappingSlot(CurrentProcess);
    if (MappingSlot < 0)
    {
        return false;
    }

    ProcessMemoryMapping& Mapping = CurrentProcess->MemoryMappings[static_cast<size_t>(MappingSlot)];
    Mapping.InUse                 = true;
    Mapping.VirtualAddressStart   = MappingStart;
    Mapping.Length                = MappingLength;
    Mapping.PhysicalAddressStart  = PhysicalStart;
    Mapping.IsAnonymous           = IsAnonymous;
    return true;
}

bool IsVirtualAddressMapped(uint64_t PageMapL4TableAddr, uint64_t Address)
{
    if (PageMapL4TableAddr == 0)
    {
        return false;
    }

    VirtualAddress Vaddr;
    Vaddr.value = Address;

    PageTableEntry* PML4 = reinterpret_cast<PageTableEntry*>(PageMapL4TableAddr);
    PageTableEntry  PML4Entry = PML4[Vaddr.fields.pml4_index];
    if (!PML4Entry.fields.present)
    {
        return false;
    }

    PageTableEntry* PDPT = reinterpret_cast<PageTableEntry*>(PML4Entry.value & PHYS_PAGE_ADDR_MASK);
    if (PDPT == nullptr)
    {
        return false;
    }

    PageTableEntry PDPTEntry = PDPT[Vaddr.fields.pdpt_index];
    if (!PDPTEntry.fields.present)
    {
        return false;
    }

    PageTableEntry* PD = reinterpret_cast<PageTableEntry*>(PDPTEntry.value & PHYS_PAGE_ADDR_MASK);
    if (PD == nullptr)
    {
        return false;
    }

    PageTableEntry PDEntry = PD[Vaddr.fields.pd_index];
    if (!PDEntry.fields.present)
    {
        return false;
    }

    PageTableEntry* PT = reinterpret_cast<PageTableEntry*>(PDEntry.value & PHYS_PAGE_ADDR_MASK);
    if (PT == nullptr)
    {
        return false;
    }

    PageTableEntry PTEntry = PT[Vaddr.fields.pt_index];
    return PTEntry.fields.present;
}

bool RangeHasAnyMappedPage(const Process* CurrentProcess, uint64_t MappingStart, uint64_t MappingLength)
{
    if (CurrentProcess == nullptr || CurrentProcess->AddressSpace == nullptr || MappingLength == 0)
    {
        return true;
    }

    uint64_t PageMapL4TableAddr = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
    if (PageMapL4TableAddr == 0)
    {
        return true;
    }

    uint64_t RangeStart = AlignDownToPageBoundary(MappingStart);
    uint64_t RangeEnd   = AlignUpToPageBoundary(MappingStart + MappingLength);
    if (RangeEnd <= RangeStart)
    {
        return true;
    }

    for (uint64_t Address = RangeStart; Address < RangeEnd; Address += PAGE_SIZE)
    {
        if (IsVirtualAddressMapped(PageMapL4TableAddr, Address))
        {
            return true;
        }
    }

    return false;
}

uint64_t FindFreeMappingAddress(const Process* CurrentProcess, uint64_t StartHint, uint64_t MappingLength)
{
    uint64_t Candidate = AlignDownToPageBoundary(StartHint);
    if (Candidate == 0)
    {
        if (CurrentProcess != nullptr && CurrentProcess->AddressSpace != nullptr)
        {
            uint64_t SuggestedStart = 0;

            if (CurrentProcess->FileType == FILE_TYPE_ELF)
            {
                const VirtualAddressSpaceELF* ELFAddressSpace = static_cast<const VirtualAddressSpaceELF*>(CurrentProcess->AddressSpace);
                const ELFMemoryRegion*        Regions         = ELFAddressSpace->GetMemoryRegions();
                size_t                        RegionCount     = ELFAddressSpace->GetMemoryRegionCount();

                uint64_t HighestELFEnd = 0;
                for (size_t RegionIndex = 0; RegionIndex < RegionCount; ++RegionIndex)
                {
                    uint64_t RegionStart = AlignDownToPageBoundary(Regions[RegionIndex].VirtualAddress);
                    uint64_t RegionEnd   = AlignUpToPageBoundary(Regions[RegionIndex].VirtualAddress + Regions[RegionIndex].Size);

                    if (RegionEnd > RegionStart && RegionEnd > HighestELFEnd)
                    {
                        HighestELFEnd = RegionEnd;
                    }
                }

                if (HighestELFEnd != 0 && HighestELFEnd <= (UINT64_MAX - PAGE_SIZE))
                {
                    SuggestedStart = HighestELFEnd + PAGE_SIZE;
                }
            }

            if (SuggestedStart == 0)
            {
                uint64_t HeapEnd = AlignUpToPageBoundary(CurrentProcess->AddressSpace->GetHeapVirtualAddressStart() + CurrentProcess->AddressSpace->GetHeapSize());
                if (HeapEnd != 0 && HeapEnd <= (UINT64_MAX - PAGE_SIZE))
                {
                    SuggestedStart = HeapEnd + PAGE_SIZE;
                }
            }

            if (SuggestedStart != 0)
            {
                Candidate = SuggestedStart;
            }
        }

        if (Candidate == 0)
        {
            Candidate = MMAP_DEFAULT_BASE;
        }
    }

    for (uint64_t Attempt = 0; Attempt < 1024 * 1024; ++Attempt)
    {
        if (!MappingOverlapsProcessLayout(CurrentProcess, Candidate, MappingLength) && !RangeHasAnyMappedPage(CurrentProcess, Candidate, MappingLength))
        {
            return Candidate;
        }

        if (Candidate > (UINT64_MAX - PAGE_SIZE))
        {
            break;
        }

        Candidate += PAGE_SIZE;
    }

    return 0;
}
} // namespace

/**
 * Function: TranslationLayer::TranslationLayer
 * Description: Constructs the translation layer with no attached logic layer.
 * Parameters:
 *   None.
 * Returns:
 *   TranslationLayer - Constructed translation layer instance.
 */
TranslationLayer::TranslationLayer() : Logic(nullptr)
{
}

/**
 * Function: TranslationLayer::Initialize
 * Description: Attaches the translation layer to the logic layer.
 * Parameters:
 *   LogicLayer* Logic - Logic layer instance used by translation layer.
 * Returns:
 *   void - Does not return a value.
 */
void TranslationLayer::Initialize(LogicLayer* Logic)
{
    this->Logic = Logic;
}

/**
 * Function: TranslationLayer::GetLogicLayer
 * Description: Returns the attached logic layer instance.
 * Parameters:
 *   None.
 * Returns:
 *   LogicLayer* - Pointer to the attached logic layer.
 */
LogicLayer* TranslationLayer::GetLogicLayer() const
{
    return Logic;
}

// Posix system call handlers

int64_t TranslationLayer::HandleReadSystemCall(uint64_t FileDescriptor, void* Buffer, uint64_t Count)
{
    if (Logic == nullptr || (Buffer == nullptr && Count != 0))
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->AccessFlags == WRITE)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->Node->FileOps == nullptr || OpenFile->Node->FileOps->Read == nullptr)
    {
        return LINUX_ERR_ENOSYS;
    }

    if (Count == 0)
    {
        return 0;
    }

    uint8_t  KernelBuffer[SYSCALL_COPY_CHUNK_SIZE];
    uint64_t TotalCopied = 0;

    while (TotalCopied < Count)
    {
        uint64_t Remaining = Count - TotalCopied;
        uint64_t ChunkSize = (Remaining < SYSCALL_COPY_CHUNK_SIZE) ? Remaining : SYSCALL_COPY_CHUNK_SIZE;

        int64_t BytesRead = OpenFile->Node->FileOps->Read(OpenFile, KernelBuffer, ChunkSize);
        if (BytesRead < 0)
        {
            return (TotalCopied == 0) ? BytesRead : static_cast<int64_t>(TotalCopied);
        }

        if (BytesRead == 0)
        {
            break;
        }

        void* UserChunkDestination = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(Buffer) + TotalCopied);
        if (!Logic->CopyFromKernelToUser(KernelBuffer, UserChunkDestination, static_cast<uint64_t>(BytesRead)))
        {
            return (TotalCopied == 0) ? LINUX_ERR_EFAULT : static_cast<int64_t>(TotalCopied);
        }

        TotalCopied += static_cast<uint64_t>(BytesRead);

        if (static_cast<uint64_t>(BytesRead) < ChunkSize)
        {
            break;
        }
    }

    return static_cast<int64_t>(TotalCopied);
}

int64_t TranslationLayer::HandleWriteSystemCall(uint64_t FileDescriptor, const void* Buffer, uint64_t Count)
{
    if (Logic == nullptr || (Buffer == nullptr && Count != 0))
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->AccessFlags == READ)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->Node->FileOps == nullptr || OpenFile->Node->FileOps->Write == nullptr)
    {
        return LINUX_ERR_ENOSYS;
    }

    if (Count == 0)
    {
        return 0;
    }

    uint8_t  KernelBuffer[SYSCALL_COPY_CHUNK_SIZE];
    uint64_t TotalCopied = 0;

    while (TotalCopied < Count)
    {
        uint64_t Remaining = Count - TotalCopied;
        uint64_t ChunkSize = (Remaining < SYSCALL_COPY_CHUNK_SIZE) ? Remaining : SYSCALL_COPY_CHUNK_SIZE;

        const void* UserChunkSource = reinterpret_cast<const void*>(reinterpret_cast<uint64_t>(Buffer) + TotalCopied);
        if (!Logic->CopyFromUserToKernel(UserChunkSource, KernelBuffer, ChunkSize))
        {
            return (TotalCopied == 0) ? LINUX_ERR_EFAULT : static_cast<int64_t>(TotalCopied);
        }

        int64_t BytesWritten = OpenFile->Node->FileOps->Write(OpenFile, KernelBuffer, ChunkSize);
        if (BytesWritten < 0)
        {
            return (TotalCopied == 0) ? BytesWritten : static_cast<int64_t>(TotalCopied);
        }

        if (BytesWritten == 0)
        {
            break;
        }

        TotalCopied += static_cast<uint64_t>(BytesWritten);

        if (static_cast<uint64_t>(BytesWritten) < ChunkSize)
        {
            break;
        }
    }

    return static_cast<int64_t>(TotalCopied);
}

int64_t TranslationLayer::HandleOpenSystemCall(const char* Path, uint64_t Flags)
{
    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Dentry* NodeDentry = VFS->Lookup(Path);
    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    if ((Flags & LINUX_O_ACCMODE) == LINUX_O_ACCMODE)
    {
        return LINUX_ERR_EINVAL;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    for (size_t FileDescriptor = 0; FileDescriptor < MAX_OPEN_FILES_PER_PROCESS; ++FileDescriptor)
    {
        if (CurrentProcess->FileTable[FileDescriptor] == nullptr)
        {
            File* NewFile                             = new File;
            NewFile->FileDescriptor                   = FileDescriptor;
            NewFile->Node                             = NodeDentry->inode;
            NewFile->CurrentOffset                    = 0;
            NewFile->AccessFlags                      = DecodeAccessFlags(Flags);
            CurrentProcess->FileTable[FileDescriptor] = NewFile;
            return static_cast<int64_t>(FileDescriptor);
        }
    }

    return LINUX_ERR_EMFILE;
}

int64_t TranslationLayer::HandleCloseSystemCall(uint64_t FileDescriptor)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
    if (OpenFile == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    delete OpenFile;
    CurrentProcess->FileTable[FileDescriptor] = nullptr;
    return 0;
}

int64_t TranslationLayer::HandleMprotectSystemCall(void* Address, uint64_t Length, int64_t Protection)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Length == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    constexpr int64_t LINUX_MPROTECT_ALLOWED_MASK = (LINUX_PROT_READ | LINUX_PROT_WRITE | LINUX_PROT_EXEC);
    if ((Protection & ~LINUX_MPROTECT_ALLOWED_MASK) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t StartAddress = reinterpret_cast<uint64_t>(Address);
    if ((StartAddress & (PAGE_SIZE - 1)) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t EndAddress = StartAddress + Length;
    if (EndAddress < StartAddress)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t ProtectedLength = AlignUpToPageBoundary(Length);
    if (ProtectedLength == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER || CurrentProcess->AddressSpace == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr || ActiveDispatcher->GetResourceLayer() == nullptr || ActiveDispatcher->GetResourceLayer()->GetPMM() == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    uint64_t UserPageTable = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
    if (UserPageTable == 0)
    {
        return LINUX_ERR_EFAULT;
    }

    bool UserAccess = (Protection != LINUX_PROT_NONE);
    bool Writeable  = (Protection & LINUX_PROT_WRITE) != 0;
    bool Executable = (Protection & LINUX_PROT_EXEC) != 0;

    VirtualMemoryManager UserVMM(UserPageTable, *ActiveDispatcher->GetResourceLayer()->GetPMM());

    uint64_t ProtectedPages = ProtectedLength / PAGE_SIZE;
    for (uint64_t PageIndex = 0; PageIndex < ProtectedPages; ++PageIndex)
    {
        uint64_t VirtualPage = StartAddress + (PageIndex * PAGE_SIZE);
        if (!UserVMM.ProtectPage(VirtualPage, UserAccess, Writeable, Executable))
        {
            return LINUX_ERR_ENOMEM;
        }
    }

    return 0;
}

int64_t TranslationLayer::HandleBrkSystemCall(uint64_t Address)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER || CurrentProcess->AddressSpace == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr || ActiveDispatcher->GetResourceLayer() == nullptr || ActiveDispatcher->GetResourceLayer()->GetPMM() == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ResourceLayer*         Resource = ActiveDispatcher->GetResourceLayer();
    PhysicalMemoryManager* PMM      = Resource->GetPMM();

    VirtualAddressSpace* AddressSpace = CurrentProcess->AddressSpace;

    uint64_t HeapStart = AddressSpace->GetHeapVirtualAddressStart();
    uint64_t HeapSize  = AddressSpace->GetHeapSize();
    uint64_t HeapEnd   = HeapStart + HeapSize;

    if (HeapEnd < HeapStart)
    {
        return LINUX_ERR_EFAULT;
    }

    if (CurrentProcess->ProgramBreak == 0)
    {
        CurrentProcess->ProgramBreak = HeapStart;
    }

    if (Address == 0)
    {
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    if (Address < HeapStart)
    {
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    uint64_t StackStart = AddressSpace->GetStackVirtualAddressStart();
    if (Address >= StackStart)
    {
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    if (Address <= HeapEnd)
    {
        CurrentProcess->ProgramBreak = Address;
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    uint64_t RequestedHeapSize = Address - HeapStart;
    uint64_t NewHeapSize       = AlignUpToPageBoundary(RequestedHeapSize);
    if (NewHeapSize == 0 || NewHeapSize < HeapSize)
    {
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    uint64_t NewHeapEnd = HeapStart + NewHeapSize;
    if (NewHeapEnd < HeapStart || NewHeapEnd > StackStart)
    {
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    uint64_t NewHeapPages = NewHeapSize / PAGE_SIZE;
    void*    NewHeapPhysical = PMM->AllocatePagesFromDescriptor(NewHeapPages);
    if (NewHeapPhysical == nullptr)
    {
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    kmemset(NewHeapPhysical, 0, static_cast<size_t>(NewHeapSize));

    uint64_t OldHeapPhysical = AddressSpace->GetHeapPhysicalAddress();
    if (OldHeapPhysical != 0 && HeapSize != 0)
    {
        memcpy(NewHeapPhysical, reinterpret_cast<const void*>(OldHeapPhysical), static_cast<size_t>(HeapSize));
    }

    uint64_t UserPageTable = AddressSpace->GetPageMapL4TableAddr();
    if (UserPageTable == 0)
    {
        PMM->FreePagesFromDescriptor(NewHeapPhysical, NewHeapPages);
        return static_cast<int64_t>(CurrentProcess->ProgramBreak);
    }

    uint64_t OldHeapPages = (HeapSize + PAGE_SIZE - 1) / PAGE_SIZE;

    VirtualMemoryManager UserVMM(UserPageTable, *PMM);
    uint64_t             MappedPages = 0;
    for (uint64_t PageIndex = 0; PageIndex < NewHeapPages; ++PageIndex)
    {
        uint64_t PhysicalPage = reinterpret_cast<uint64_t>(NewHeapPhysical) + (PageIndex * PAGE_SIZE);
        uint64_t VirtualPage  = HeapStart + (PageIndex * PAGE_SIZE);
        if (!UserVMM.MapPage(PhysicalPage, VirtualPage, PageMappingFlags(true, true)))
        {
            for (uint64_t RollbackIndex = 0; RollbackIndex < MappedPages; ++RollbackIndex)
            {
                uint64_t RollbackVirtualPage = HeapStart + (RollbackIndex * PAGE_SIZE);
                if (RollbackIndex < OldHeapPages && OldHeapPhysical != 0)
                {
                    uint64_t OldPhysicalPage = OldHeapPhysical + (RollbackIndex * PAGE_SIZE);
                    UserVMM.MapPage(OldPhysicalPage, RollbackVirtualPage, PageMappingFlags(true, true));
                }
                else
                {
                    UserVMM.UnmapPage(RollbackVirtualPage);
                }
            }

            PMM->FreePagesFromDescriptor(NewHeapPhysical, NewHeapPages);
            return static_cast<int64_t>(CurrentProcess->ProgramBreak);
        }

        ++MappedPages;
    }

    if (OldHeapPhysical != 0 && HeapSize != 0)
    {
        PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(OldHeapPhysical), OldHeapPages);
    }

    AddressSpace->SetHeapPhysicalAddress(reinterpret_cast<uint64_t>(NewHeapPhysical));
    AddressSpace->SetHeapSize(NewHeapSize);

    uint64_t ActivePageTable = Resource->ReadCurrentPageTable();
    if (ActivePageTable == UserPageTable)
    {
        Resource->LoadPageTable(ActivePageTable);
    }

    CurrentProcess->ProgramBreak = Address;
    return static_cast<int64_t>(CurrentProcess->ProgramBreak);
}

int64_t TranslationLayer::HandleDup2SystemCall(uint64_t OldFileDescriptor, uint64_t NewFileDescriptor)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (OldFileDescriptor >= MAX_OPEN_FILES_PER_PROCESS || NewFileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* SourceFile = CurrentProcess->FileTable[OldFileDescriptor];
    if (SourceFile == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (OldFileDescriptor == NewFileDescriptor)
    {
        return static_cast<int64_t>(NewFileDescriptor);
    }

    File* ExistingTargetFile = CurrentProcess->FileTable[NewFileDescriptor];
    if (ExistingTargetFile != nullptr)
    {
        delete ExistingTargetFile;
        CurrentProcess->FileTable[NewFileDescriptor] = nullptr;
    }

    File* DuplicatedFile = new File;
    if (DuplicatedFile == nullptr)
    {
        return LINUX_ERR_ENOMEM;
    }

    *DuplicatedFile              = *SourceFile;
    DuplicatedFile->FileDescriptor = NewFileDescriptor;
    CurrentProcess->FileTable[NewFileDescriptor] = DuplicatedFile;

    return static_cast<int64_t>(NewFileDescriptor);
}

int64_t TranslationLayer::HandleForkSystemCall()
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    uint8_t ChildId = Logic->CopyProcess(CurrentProcess->Id);
    if (ChildId == PROCESS_ID_INVALID)
    {
        return LINUX_ERR_EAGAIN;
    }

    Process* ChildProcess = PM->GetProcessById(ChildId);
    if (ChildProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (!CurrentProcess->HasSavedSystemCallFrame)
    {
        return LINUX_ERR_EFAULT;
    }

    const ProcessSavedSystemCallFrame& SavedFrame = CurrentProcess->SavedSystemCallFrame;

    ChildProcess->State.rax    = SavedFrame.UserRAX;
    ChildProcess->State.rcx    = 0;
    ChildProcess->State.rdx    = SavedFrame.UserRDX;
    ChildProcess->State.rbx    = SavedFrame.UserRBX;
    ChildProcess->State.rbp    = SavedFrame.UserRBP;
    ChildProcess->State.rsi    = SavedFrame.UserRSI;
    ChildProcess->State.rdi    = SavedFrame.UserRDI;
    ChildProcess->State.r8     = SavedFrame.UserR8;
    ChildProcess->State.r9     = SavedFrame.UserR9;
    ChildProcess->State.r10    = SavedFrame.UserR10;
    ChildProcess->State.r11    = 0;
    ChildProcess->State.r12    = SavedFrame.UserR12;
    ChildProcess->State.r13    = SavedFrame.UserR13;
    ChildProcess->State.r14    = SavedFrame.UserR14;
    ChildProcess->State.r15    = SavedFrame.UserR15;
    ChildProcess->State.rip    = SavedFrame.UserRIP;
    ChildProcess->State.rflags = SavedFrame.UserRFLAGS;
    ChildProcess->State.rsp    = SavedFrame.UserRSP;
    ChildProcess->State.cs     = USER_CS;
    ChildProcess->State.ss     = USER_SS;
    ChildProcess->State.rax = 0;

    return static_cast<int64_t>(ChildId);
}

int64_t TranslationLayer::HandleExecveSystemCall(const char* Path, const char* const* Argv, const char* const* Envp)
{
    if (Logic == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char* KernelPathBuffer = reinterpret_cast<char*>(Logic->kmalloc(SYSCALL_PATH_MAX));
    if (KernelPathBuffer == nullptr)
    {
        return LINUX_ERR_ENOMEM;
    }

    if (!CopyUserCString(Logic, Path, KernelPathBuffer, SYSCALL_PATH_MAX))
    {
        Logic->kfree(KernelPathBuffer);
        return LINUX_ERR_EFAULT;
    }

    char**   KernelArgv  = nullptr;
    char**   KernelEnvp  = nullptr;
    uint64_t KernelArgc  = 0;
    uint64_t KernelEnvc  = 0;
    bool     IsArgvValid = CopyUserStringVector(Logic, Argv, &KernelArgv, &KernelArgc);
    bool     IsEnvpValid = IsArgvValid && CopyUserStringVector(Logic, Envp, &KernelEnvp, &KernelEnvc);

    if (!IsEnvpValid)
    {
        if (KernelArgv != nullptr)
        {
            FreeKernelStringVector(Logic, KernelArgv, KernelArgc);
        }

        if (KernelEnvp != nullptr)
        {
            FreeKernelStringVector(Logic, KernelEnvp, KernelEnvc);
        }

        Logic->kfree(KernelPathBuffer);
        return LINUX_ERR_EFAULT;
    }

    uint8_t ChangedProcessId = Logic->ChangeProcessExecution(CurrentProcess->Id, KernelPathBuffer, KernelArgv, KernelArgc, KernelEnvp, KernelEnvc);

    if (KernelArgv != nullptr)
    {
        FreeKernelStringVector(Logic, KernelArgv, KernelArgc);
    }

    if (KernelEnvp != nullptr)
    {
        FreeKernelStringVector(Logic, KernelEnvp, KernelEnvc);
    }

    Logic->kfree(KernelPathBuffer);

    if (ChangedProcessId == PROCESS_ID_INVALID)
    {
        return LINUX_ERR_ENOENT;
    }

    return 0;
}

int64_t TranslationLayer::HandleWaitSystemCall(int* Status)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    auto ConsumePendingChild = [&]() -> int64_t
    {
        if (!CurrentProcess->HasPendingChildExit)
        {
            return PROCESS_ID_INVALID;
        }

        int32_t ChildExitStatus = CurrentProcess->PendingChildStatus;
        if (Status != nullptr && !Logic->CopyFromKernelToUser(&ChildExitStatus, Status, sizeof(ChildExitStatus)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint8_t ExitedChildId               = CurrentProcess->PendingChildId;
        CurrentProcess->HasPendingChildExit = false;
        CurrentProcess->PendingChildId      = PROCESS_ID_INVALID;
        CurrentProcess->PendingChildStatus  = 0;
        CurrentProcess->WaitingForChild     = false;
        return static_cast<int64_t>(ExitedChildId);
    };

    int64_t ImmediateResult = ConsumePendingChild();
    if (ImmediateResult != PROCESS_ID_INVALID)
    {
        return ImmediateResult;
    }

    if (!ProcessHasLiveChild(PM, CurrentProcess->Id))
    {
        return LINUX_ERR_ECHILD;
    }

    CurrentProcess->WaitingForChild = true;
    Logic->BlockProcess(CurrentProcess->Id);

    int64_t ResultAfterWake = ConsumePendingChild();
    if (ResultAfterWake != PROCESS_ID_INVALID)
    {
        return ResultAfterWake;
    }

    CurrentProcess->WaitingForChild = false;
    return LINUX_ERR_ECHILD;
}

int64_t TranslationLayer::HandleMmapSystemCall(void* Address, uint64_t Length, int64_t Protection, int64_t Flags, int64_t FileDescriptor, int64_t Offset)
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();

    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Length == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Offset < 0 || (static_cast<uint64_t>(Offset) & (PAGE_SIZE - 1)) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    int64_t SharingType = Flags & (LINUX_MAP_PRIVATE | LINUX_MAP_SHARED);
    if (SharingType == 0 || SharingType == (LINUX_MAP_PRIVATE | LINUX_MAP_SHARED))
    {
        return LINUX_ERR_EINVAL;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER || CurrentProcess->AddressSpace == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t MappingLength = AlignUpToPageBoundary(Length);
    if (MappingLength == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t RequestedAddress = reinterpret_cast<uint64_t>(Address);
    uint64_t MappingStart     = 0;

    if ((Flags & LINUX_MAP_FIXED) != 0)
    {
        if ((RequestedAddress & (PAGE_SIZE - 1)) != 0)
        {
            return LINUX_ERR_EINVAL;
        }

        MappingStart = RequestedAddress;
        if (MappingOverlapsProcessLayout(CurrentProcess, MappingStart, MappingLength))
        {
            return LINUX_ERR_EINVAL;
        }
    }
    else
    {
        MappingStart = FindFreeMappingAddress(CurrentProcess, RequestedAddress, MappingLength);
        if (MappingStart == 0)
        {
            return LINUX_ERR_ENOMEM;
        }
    }

    if ((Flags & LINUX_MAP_ANONYMOUS) != 0)
    {
        if (ActiveDispatcher == nullptr || ActiveDispatcher->GetResourceLayer() == nullptr || ActiveDispatcher->GetResourceLayer()->GetPMM() == nullptr)
        {
            return LINUX_ERR_EFAULT;
        }

        PhysicalMemoryManager* PMM      = ActiveDispatcher->GetResourceLayer()->GetPMM();
        uint64_t               PageCount = MappingLength / PAGE_SIZE;
        void*                  PhysicalAllocation = PMM->AllocatePagesFromDescriptor(PageCount);
        if (PhysicalAllocation == nullptr)
        {
            return LINUX_ERR_ENOMEM;
        }

        kmemset(PhysicalAllocation, 0, static_cast<size_t>(PageCount * PAGE_SIZE));

        uint64_t PageMapL4TableAddr = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
        if (PageMapL4TableAddr == 0)
        {
            PMM->FreePagesFromDescriptor(PhysicalAllocation, PageCount);
            return LINUX_ERR_EFAULT;
        }

        VirtualMemoryManager UserVMM(PageMapL4TableAddr, *PMM);
        bool                 Writable = (Protection & LINUX_PROT_WRITE) != 0;

        for (uint64_t PageIndex = 0; PageIndex < PageCount; ++PageIndex)
        {
            uint64_t PhysicalPage = reinterpret_cast<uint64_t>(PhysicalAllocation) + (PageIndex * PAGE_SIZE);
            uint64_t VirtualPage  = MappingStart + (PageIndex * PAGE_SIZE);
            if (!UserVMM.MapPage(PhysicalPage, VirtualPage, PageMappingFlags(true, Writable)))
            {
                PMM->FreePagesFromDescriptor(PhysicalAllocation, PageCount);
                return LINUX_ERR_EFAULT;
            }
        }

        if (!RegisterProcessMapping(CurrentProcess, MappingStart, MappingLength, reinterpret_cast<uint64_t>(PhysicalAllocation), true))
        {
            PMM->FreePagesFromDescriptor(PhysicalAllocation, PageCount);
            return LINUX_ERR_ENOMEM;
        }

        uint64_t ActivePageTable = ActiveDispatcher->GetResourceLayer()->ReadCurrentPageTable();
        if (ActivePageTable == PageMapL4TableAddr)
        {
            ActiveDispatcher->GetResourceLayer()->LoadPageTable(ActivePageTable);
        }

        return static_cast<int64_t>(MappingStart);
    }

    if (FileDescriptor < 0 || static_cast<uint64_t>(FileDescriptor) >= MAX_OPEN_FILES_PER_PROCESS)
    {
        return LINUX_ERR_EBADF;
    }

    File* OpenFile = CurrentProcess->FileTable[static_cast<size_t>(FileDescriptor)];
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->Node->FileOps == nullptr || OpenFile->Node->FileOps->MemoryMap == nullptr)
    {
        return LINUX_ERR_ENODEV;
    }

    uint64_t FileMappedAddress = 0;
    int64_t  MappingResult     = OpenFile->Node->FileOps->MemoryMap(OpenFile, Length, static_cast<uint64_t>(Offset), CurrentProcess->AddressSpace, &FileMappedAddress);
    if (MappingResult < 0)
    {
        return MappingResult;
    }

    if (FileMappedAddress == 0)
    {
        return LINUX_ERR_EFAULT;
    }

    uint64_t RegisteredStart  = AlignDownToPageBoundary(FileMappedAddress);
    uint64_t RegisteredLength = AlignUpToPageBoundary((FileMappedAddress - RegisteredStart) + Length);
    if (RegisteredLength == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    if (!RegisterProcessMapping(CurrentProcess, RegisteredStart, RegisteredLength, 0, false))
    {
        return LINUX_ERR_ENOMEM;
    }

    if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr)
    {
        uint64_t UserPageTable = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
        uint64_t ActivePageTable = ActiveDispatcher->GetResourceLayer()->ReadCurrentPageTable();
        if (UserPageTable != 0 && ActivePageTable == UserPageTable)
        {
            ActiveDispatcher->GetResourceLayer()->LoadPageTable(ActivePageTable);
        }
    }

    return static_cast<int64_t>(FileMappedAddress);
}

int64_t TranslationLayer::HandleArchPrctlSystemCall(uint64_t Code, uint64_t Address)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Code != LINUX_ARCH_SET_FS)
    {
        return LINUX_ERR_EINVAL;
    }

    if (!IsCanonicalX86_64Address(Address))
    {
        return LINUX_ERR_EPERM;
    }

    CurrentProcess->UserFSBase = Address;
    SetUserFSBase(Address);
    return 0;
}

int64_t TranslationLayer::HandleSetTidAddressSystemCall(int* TidPointer)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER)
    {
        return LINUX_ERR_EINVAL;
    }

    CurrentProcess->ClearChildTidAddress = TidPointer;
    return static_cast<int64_t>(CurrentProcess->Id);
}