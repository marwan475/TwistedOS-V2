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
#include <Layers/Resource/TTY.hpp>
#include <Layers/Resource/VirtualAddressSpace.hpp>
#include <Memory/VirtualMemoryManager.hpp>

namespace
{
constexpr int64_t LINUX_ERR_EFAULT  = -14;
constexpr int64_t LINUX_ERR_ENOENT  = -2;
constexpr int64_t LINUX_ERR_ENOMEM  = -12;
constexpr int64_t LINUX_ERR_EAGAIN  = -11;
constexpr int64_t LINUX_ERR_EMFILE  = -24;
constexpr int64_t LINUX_ERR_EINVAL  = -22;
constexpr int64_t LINUX_ERR_EBADF   = -9;
constexpr int64_t LINUX_ERR_ENOSYS  = -38;
constexpr int64_t LINUX_ERR_ENOTTY  = -25;
constexpr int64_t LINUX_ERR_ECHILD  = -10;
constexpr int64_t LINUX_ERR_ENODEV  = -19;
constexpr int64_t LINUX_ERR_ENOTDIR = -20;
constexpr int64_t LINUX_ERR_EPERM   = -1;
constexpr int64_t LINUX_ERR_ERANGE  = -34;

constexpr uint64_t SYSCALL_COPY_CHUNK_SIZE   = 4096;
constexpr uint64_t SYSCALL_PATH_MAX          = 4096;
constexpr uint64_t SYSCALL_EXEC_MAX_VECTOR   = 128;
constexpr uint64_t SYSCALL_MAX_PATH_SEGMENTS = 256;
constexpr uint64_t SYSCALL_IOV_MAX           = 1024;

struct LinuxIOVec
{
    uint64_t Base;
    uint64_t Length;
};

struct LinuxStat
{
    uint64_t Device;
    uint64_t Inode;
    uint64_t HardLinkCount;
    uint32_t Mode;
    uint32_t UserId;
    uint32_t GroupId;
    uint32_t Padding0;
    uint64_t SpecialDevice;
    int64_t  Size;
    int64_t  BlockSize;
    int64_t  Blocks;
    uint64_t AccessTimeSeconds;
    uint64_t AccessTimeNanoseconds;
    uint64_t ModifyTimeSeconds;
    uint64_t ModifyTimeNanoseconds;
    uint64_t ChangeTimeSeconds;
    uint64_t ChangeTimeNanoseconds;
    int64_t  Reserved[3];
};

constexpr uint32_t LINUX_S_IFREG = 0100000;
constexpr uint32_t LINUX_S_IFDIR = 0040000;
constexpr uint32_t LINUX_S_IFLNK = 0120000;
constexpr uint32_t LINUX_S_IFCHR = 0020000;

constexpr uint32_t LINUX_DEFAULT_FILE_PERMISSIONS = 0755;
constexpr uint32_t LINUX_DEFAULT_DIR_PERMISSIONS  = 0755;
constexpr uint32_t LINUX_DEFAULT_LINK_PERMISSIONS = 0777;
constexpr uint32_t LINUX_DEFAULT_CHAR_PERMISSIONS = 0666;

constexpr int64_t LINUX_MAP_SHARED    = 0x01;
constexpr int64_t LINUX_MAP_PRIVATE   = 0x02;
constexpr int64_t LINUX_MAP_FIXED     = 0x10;
constexpr int64_t LINUX_MAP_ANONYMOUS = 0x20;

constexpr int64_t LINUX_PROT_READ  = 0x1;
constexpr int64_t LINUX_PROT_WRITE = 0x2;
constexpr int64_t LINUX_PROT_EXEC  = 0x4;
constexpr int64_t LINUX_PROT_NONE  = 0x0;

constexpr uint64_t MMAP_DEFAULT_BASE = 0x0000000001000000;

constexpr uint64_t LINUX_O_ACCMODE  = 0x3;
constexpr uint64_t LINUX_O_APPEND   = 0x400;
constexpr uint64_t LINUX_O_NONBLOCK = 0x800;
constexpr uint64_t LINUX_O_ASYNC    = 0x2000;
constexpr uint64_t LINUX_O_DIRECT   = 0x4000;
constexpr uint64_t LINUX_O_NOATIME  = 0x40000;
constexpr uint64_t LINUX_O_CLOEXEC  = 0x80000;

constexpr uint64_t LINUX_F_DUPFD         = 0;
constexpr uint64_t LINUX_F_GETFD         = 1;
constexpr uint64_t LINUX_F_SETFD         = 2;
constexpr uint64_t LINUX_F_GETFL         = 3;
constexpr uint64_t LINUX_F_SETFL         = 4;
constexpr uint64_t LINUX_F_DUPFD_CLOEXEC = 1030;
constexpr uint64_t LINUX_FD_CLOEXEC      = 0x1;

constexpr uint64_t LINUX_FCNTL_SETFL_ALLOWED = (LINUX_O_APPEND | LINUX_O_NONBLOCK | LINUX_O_ASYNC | LINUX_O_DIRECT | LINUX_O_NOATIME);

constexpr int64_t LINUX_AT_FDCWD            = -100;
constexpr int64_t LINUX_AT_SYMLINK_NOFOLLOW = 0x100;
constexpr int64_t LINUX_AT_NO_AUTOMOUNT     = 0x800;
constexpr int64_t LINUX_AT_EMPTY_PATH       = 0x1000;

constexpr uint8_t LINUX_DT_UNKNOWN = 0;
constexpr uint8_t LINUX_DT_CHR     = 2;
constexpr uint8_t LINUX_DT_DIR     = 4;
constexpr uint8_t LINUX_DT_REG     = 8;
constexpr uint8_t LINUX_DT_LNK     = 10;

constexpr uint64_t LINUX_ARCH_SET_FS = 0x1002;
constexpr uint64_t LINUX_ARCH_GET_FS = 0x1003;

constexpr int64_t LINUX_SIG_BLOCK   = 0;
constexpr int64_t LINUX_SIG_UNBLOCK = 1;
constexpr int64_t LINUX_SIG_SETMASK = 2;

constexpr int64_t LINUX_SIG_DFL = 0;
constexpr int64_t LINUX_SIG_IGN = 1;

constexpr uint64_t LINUX_RT_SIGSET_SIZE    = sizeof(uint64_t);
constexpr uint64_t LINUX_RT_SIGACTION_SIZE = sizeof(uint64_t) * 4;
constexpr int64_t  LINUX_SIGNAL_MIN        = 1;
constexpr int64_t  LINUX_SIGNAL_MAX        = static_cast<int64_t>(MAX_POSIX_SIGNALS_PER_PROCESS);

constexpr uint64_t LINUX_SIGKILL_MASK            = (1ULL << (9 - 1));
constexpr uint64_t LINUX_SIGSTOP_MASK            = (1ULL << (19 - 1));
constexpr uint64_t LINUX_UNBLOCKABLE_SIGNAL_MASK = (LINUX_SIGKILL_MASK | LINUX_SIGSTOP_MASK);

constexpr int64_t LINUX_SIGNAL_SIGKILL = 9;
constexpr int64_t LINUX_SIGNAL_SIGSTOP = 19;

struct LinuxKernelSigAction
{
    uint64_t Handler;
    uint64_t Flags;
    uint64_t Restorer;
    uint64_t Mask;
};

bool IsCanonicalX86_64Address(uint64_t Address)
{
    constexpr uint64_t LOWER_CANONICAL_MAX = 0x00007FFFFFFFFFFFULL;
    constexpr uint64_t UPPER_CANONICAL_MIN = 0xFFFF800000000000ULL;
    return (Address <= LOWER_CANONICAL_MAX) || (Address >= UPPER_CANONICAL_MIN);
}

struct __attribute__((packed)) LinuxDirent64Header
{
    uint64_t Inode;
    uint64_t Offset;
    uint16_t RecordLength;
    uint8_t  Type;
};

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

uint32_t BuildLinuxModeFromNode(const INode* Node)
{
    if (Node == nullptr)
    {
        return 0;
    }

    switch (Node->NodeType)
    {
        case INODE_DIR:
            return (LINUX_S_IFDIR | LINUX_DEFAULT_DIR_PERMISSIONS);
        case INODE_SYMLINK:
            return (LINUX_S_IFLNK | LINUX_DEFAULT_LINK_PERMISSIONS);
        case INODE_DEV:
            return (LINUX_S_IFCHR | LINUX_DEFAULT_CHAR_PERMISSIONS);
        case INODE_FILE:
        default:
            return (LINUX_S_IFREG | LINUX_DEFAULT_FILE_PERMISSIONS);
    }
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

uint64_t AlignUpValue(uint64_t Value, uint64_t Alignment)
{
    if (Alignment == 0)
    {
        return Value;
    }

    uint64_t Remainder = Value % Alignment;
    if (Remainder == 0)
    {
        return Value;
    }

    return Value + (Alignment - Remainder);
}

uint8_t BuildLinuxDirentTypeFromNode(const INode* Node)
{
    if (Node == nullptr)
    {
        return LINUX_DT_UNKNOWN;
    }

    switch (Node->NodeType)
    {
        case INODE_DIR:
            return LINUX_DT_DIR;
        case INODE_SYMLINK:
            return LINUX_DT_LNK;
        case INODE_DEV:
            return LINUX_DT_CHR;
        case INODE_FILE:
        default:
            return LINUX_DT_REG;
    }
}

void PopulateLinuxStatFromNode(const INode* Node, LinuxStat* KernelStat)
{
    if (Node == nullptr || KernelStat == nullptr)
    {
        return;
    }

    *KernelStat                       = {};
    KernelStat->Device                = 1;
    KernelStat->Inode                 = reinterpret_cast<uint64_t>(Node);
    KernelStat->HardLinkCount         = (Node->NodeType == INODE_DIR) ? 2 : 1;
    KernelStat->Mode                  = BuildLinuxModeFromNode(Node);
    KernelStat->UserId                = 0;
    KernelStat->GroupId               = 0;
    KernelStat->Padding0              = 0;
    KernelStat->SpecialDevice         = (Node->NodeType == INODE_DEV) ? KernelStat->Inode : 0;
    KernelStat->Size                  = static_cast<int64_t>(Node->NodeSize);
    KernelStat->BlockSize             = static_cast<int64_t>(PAGE_SIZE);
    KernelStat->Blocks                = static_cast<int64_t>((Node->NodeSize + 511) / 512);
    KernelStat->AccessTimeSeconds     = 0;
    KernelStat->AccessTimeNanoseconds = 0;
    KernelStat->ModifyTimeSeconds     = 0;
    KernelStat->ModifyTimeNanoseconds = 0;
    KernelStat->ChangeTimeSeconds     = 0;
    KernelStat->ChangeTimeNanoseconds = 0;
}

int64_t AllocateProcessFileDescriptor(Process* CurrentProcess, Dentry* NodeDentry, uint64_t Flags)
{
    if (CurrentProcess == nullptr || NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if ((Flags & LINUX_O_ACCMODE) == LINUX_O_ACCMODE)
    {
        return LINUX_ERR_EINVAL;
    }

    for (size_t FileDescriptor = 0; FileDescriptor < MAX_OPEN_FILES_PER_PROCESS; ++FileDescriptor)
    {
        if (CurrentProcess->FileTable[FileDescriptor] == nullptr)
        {
            File* NewFile = new File;
            if (NewFile == nullptr)
            {
                return LINUX_ERR_ENOMEM;
            }

            NewFile->FileDescriptor  = FileDescriptor;
            NewFile->Node            = NodeDentry->inode;
            NewFile->CurrentOffset   = 0;
            NewFile->AccessFlags     = DecodeAccessFlags(Flags);
            NewFile->OpenFlags       = Flags;
            NewFile->DescriptorFlags = ((Flags & LINUX_O_CLOEXEC) != 0) ? LINUX_FD_CLOEXEC : 0;
            NewFile->DirectoryEntry  = NodeDentry;

            CurrentProcess->FileTable[FileDescriptor] = NewFile;
            return static_cast<int64_t>(FileDescriptor);
        }
    }

    return LINUX_ERR_EMFILE;
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
        const char* UserEntry        = nullptr;
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

bool BuildAbsolutePathFromDentry(const Dentry* Node, char* Buffer, uint64_t BufferSize)
{
    if (Node == nullptr || Buffer == nullptr || BufferSize == 0)
    {
        return false;
    }

    const char* SegmentStack[SYSCALL_MAX_PATH_SEGMENTS] = {};
    uint64_t    SegmentCount                            = 0;

    const Dentry* Current = Node;
    while (Current != nullptr && Current->parent != nullptr)
    {
        if (SegmentCount >= SYSCALL_MAX_PATH_SEGMENTS)
        {
            return false;
        }

        if (Current->name == nullptr)
        {
            return false;
        }

        SegmentStack[SegmentCount++] = Current->name;
        Current                      = Current->parent;
    }

    uint64_t Cursor  = 0;
    Buffer[Cursor++] = '/';

    if (SegmentCount == 0)
    {
        if (Cursor >= BufferSize)
        {
            return false;
        }

        Buffer[Cursor] = '\0';
        return true;
    }

    for (uint64_t SegmentIndex = SegmentCount; SegmentIndex > 0; --SegmentIndex)
    {
        const char* Segment       = SegmentStack[SegmentIndex - 1];
        uint64_t    SegmentLength = CStrLength(Segment);
        if (SegmentLength == 0)
        {
            continue;
        }

        if ((Cursor + SegmentLength) >= BufferSize)
        {
            return false;
        }

        memcpy(Buffer + Cursor, Segment, static_cast<size_t>(SegmentLength));
        Cursor += SegmentLength;

        if (SegmentIndex != 1)
        {
            if (Cursor >= BufferSize)
            {
                return false;
            }

            Buffer[Cursor++] = '/';
        }
    }

    if (Cursor >= BufferSize)
    {
        return false;
    }

    Buffer[Cursor] = '\0';
    return true;
}

void ReleaseVforkParentIfNeeded(LogicLayer* Logic, Process* ChildProcess)
{
    if (Logic == nullptr || ChildProcess == nullptr || !ChildProcess->IsVforkChild)
    {
        return;
    }

    ProcessManager* PM = Logic->GetProcessManager();
    if (PM == nullptr)
    {
        return;
    }

    uint8_t ParentId = ChildProcess->VforkParentId;

    ChildProcess->IsVforkChild  = false;
    ChildProcess->VforkParentId = PROCESS_ID_INVALID;

    Process* ParentProcess = PM->GetProcessById(ParentId);
    if (ParentProcess == nullptr || ParentProcess->Status == PROCESS_TERMINATED)
    {
        return;
    }

    bool ShouldUnblockParent            = ParentProcess->WaitingForVforkChild && ParentProcess->VforkChildId == ChildProcess->Id;
    ParentProcess->WaitingForVforkChild = false;
    ParentProcess->VforkChildId         = PROCESS_ID_INVALID;

    if (ShouldUnblockParent)
    {
        Logic->UnblockProcess(ParentProcess->Id);
    }
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

    PageTableEntry* PML4      = reinterpret_cast<PageTableEntry*>(PageMapL4TableAddr);
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

int64_t TranslationLayer::HandleWritevSystemCall(uint64_t FileDescriptor, const void* Iov, uint64_t IovCount)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (IovCount == 0 || IovCount > SYSCALL_IOV_MAX)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Iov == nullptr)
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

    uint64_t TotalWritten = 0;

    for (uint64_t Index = 0; Index < IovCount; ++Index)
    {
        LinuxIOVec  KernelIOVec      = {};
        const void* UserIOVecAddress = reinterpret_cast<const void*>(reinterpret_cast<uint64_t>(Iov) + (Index * sizeof(LinuxIOVec)));
        if (!Logic->CopyFromUserToKernel(UserIOVecAddress, &KernelIOVec, sizeof(KernelIOVec)))
        {
            return (TotalWritten == 0) ? LINUX_ERR_EFAULT : static_cast<int64_t>(TotalWritten);
        }

        if (KernelIOVec.Length == 0)
        {
            continue;
        }

        if (KernelIOVec.Base == 0)
        {
            return (TotalWritten == 0) ? LINUX_ERR_EFAULT : static_cast<int64_t>(TotalWritten);
        }

        uint8_t  KernelBuffer[SYSCALL_COPY_CHUNK_SIZE];
        uint64_t SegmentWritten = 0;

        while (SegmentWritten < KernelIOVec.Length)
        {
            uint64_t Remaining = KernelIOVec.Length - SegmentWritten;
            uint64_t ChunkSize = (Remaining < SYSCALL_COPY_CHUNK_SIZE) ? Remaining : SYSCALL_COPY_CHUNK_SIZE;

            const void* UserChunkSource = reinterpret_cast<const void*>(KernelIOVec.Base + SegmentWritten);
            if (!Logic->CopyFromUserToKernel(UserChunkSource, KernelBuffer, ChunkSize))
            {
                return (TotalWritten == 0) ? LINUX_ERR_EFAULT : static_cast<int64_t>(TotalWritten);
            }

            int64_t BytesWritten = OpenFile->Node->FileOps->Write(OpenFile, KernelBuffer, ChunkSize);
            if (BytesWritten < 0)
            {
                return (TotalWritten == 0) ? BytesWritten : static_cast<int64_t>(TotalWritten);
            }

            if (BytesWritten == 0)
            {
                break;
            }

            SegmentWritten += static_cast<uint64_t>(BytesWritten);

            if (static_cast<uint64_t>(BytesWritten) < ChunkSize)
            {
                break;
            }
        }

        if (SegmentWritten == 0)
        {
            break;
        }

        if (TotalWritten > (UINT64_MAX - SegmentWritten))
        {
            return (TotalWritten == 0) ? LINUX_ERR_EINVAL : static_cast<int64_t>(TotalWritten);
        }

        TotalWritten += SegmentWritten;

        if (SegmentWritten < KernelIOVec.Length)
        {
            break;
        }
    }

    return static_cast<int64_t>(TotalWritten);
}

int64_t TranslationLayer::HandleIoctlSystemCall(uint64_t FileDescriptor, uint64_t Request, uint64_t Argument)
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
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EBADF;
    }

    if (OpenFile->Node->FileOps == nullptr || OpenFile->Node->FileOps->Ioctl == nullptr)
    {
        return LINUX_ERR_ENOTTY;
    }

    return OpenFile->Node->FileOps->Ioctl(OpenFile, Request, Argument, Logic);
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

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char        EffectivePath[SYSCALL_PATH_MAX] = {};
    const char* LookupPath                      = KernelPath;

    if (KernelPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectivePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        LookupPath                             = EffectivePath;
    }

    Dentry* NodeDentry = VFS->Lookup(LookupPath);
    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    return AllocateProcessFileDescriptor(CurrentProcess, NodeDentry, Flags);
}

int64_t TranslationLayer::HandleOpenAtSystemCall(int64_t DirectoryFileDescriptor, const char* Path, uint64_t Flags, uint64_t Mode)
{
    (void) Mode;

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

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    if (KernelPath[0] == '\0')
    {
        return LINUX_ERR_ENOENT;
    }

    Dentry* NodeDentry = nullptr;

    if (KernelPath[0] == '/')
    {
        NodeDentry = VFS->Lookup(KernelPath);
    }
    else
    {
        Dentry* BaseDirectory = nullptr;

        if (DirectoryFileDescriptor == LINUX_AT_FDCWD)
        {
            BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        }
        else
        {
            if (DirectoryFileDescriptor < 0 || static_cast<uint64_t>(DirectoryFileDescriptor) >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* DirectoryFile = CurrentProcess->FileTable[static_cast<size_t>(DirectoryFileDescriptor)];
            if (DirectoryFile == nullptr || DirectoryFile->Node == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            if (DirectoryFile->Node->NodeType != INODE_DIR)
            {
                return LINUX_ERR_ENOTDIR;
            }

            BaseDirectory = DirectoryFile->DirectoryEntry;
        }

        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        char     AbsolutePath[SYSCALL_PATH_MAX] = {};
        uint64_t BasePathLength                 = CStrLength(BasePath);
        uint64_t RelativeLength                 = CStrLength(KernelPath);
        uint64_t NeedsSeparator                 = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes                  = BasePathLength + NeedsSeparator + RelativeLength + 1;

        if (RequiredBytes > sizeof(AbsolutePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(AbsolutePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            AbsolutePath[Cursor++] = '/';
        }

        memcpy(AbsolutePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        AbsolutePath[Cursor + RelativeLength] = '\0';

        NodeDentry = VFS->Lookup(AbsolutePath);
    }

    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    return AllocateProcessFileDescriptor(CurrentProcess, NodeDentry, Flags);
}

int64_t TranslationLayer::HandleStatSystemCall(const char* Path, void* Buffer)
{
    if (Logic == nullptr || Path == nullptr || Buffer == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    if (VFS == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
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

    char        EffectivePath[SYSCALL_PATH_MAX] = {};
    const char* LookupPath                      = KernelPath;

    if (KernelPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectivePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        LookupPath                             = EffectivePath;
    }

    Dentry* NodeDentry = VFS->Lookup(LookupPath);
    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    LinuxStat KernelStat = {};
    PopulateLinuxStatFromNode(NodeDentry->inode, &KernelStat);

    if (!Logic->CopyFromKernelToUser(&KernelStat, Buffer, sizeof(KernelStat)))
    {
        return LINUX_ERR_EFAULT;
    }

    return 0;
}

int64_t TranslationLayer::HandleLstatSystemCall(const char* Path, void* Buffer)
{
    if (Logic == nullptr || Path == nullptr || Buffer == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    if (VFS == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
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

    char        EffectivePath[SYSCALL_PATH_MAX] = {};
    const char* LookupPath                      = KernelPath;

    if (KernelPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectivePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        LookupPath                             = EffectivePath;
    }

    Dentry* NodeDentry = VFS->LookupNoFollowFinal(LookupPath);
    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    LinuxStat KernelStat = {};
    PopulateLinuxStatFromNode(NodeDentry->inode, &KernelStat);

    if (!Logic->CopyFromKernelToUser(&KernelStat, Buffer, sizeof(KernelStat)))
    {
        return LINUX_ERR_EFAULT;
    }

    return 0;
}

int64_t TranslationLayer::HandleNewFstatatSystemCall(int64_t DirectoryFileDescriptor, const char* Path, void* Buffer, int64_t Flags)
{
    if (Logic == nullptr || Buffer == nullptr || Path == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    int64_t AllowedFlags = LINUX_AT_SYMLINK_NOFOLLOW | LINUX_AT_NO_AUTOMOUNT | LINUX_AT_EMPTY_PATH;
    if ((Flags & ~AllowedFlags) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    VirtualFileSystem* VFS = Logic->GetVirtualFileSystem();
    ProcessManager*    PM  = Logic->GetProcessManager();
    if (VFS == nullptr || PM == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    Dentry* NodeDentry = nullptr;

    if (KernelPath[0] == '\0' && (Flags & LINUX_AT_EMPTY_PATH) != 0)
    {
        if (DirectoryFileDescriptor == LINUX_AT_FDCWD)
        {
            NodeDentry = CurrentProcess->CurrentFileSystemLocation;
        }
        else
        {
            if (DirectoryFileDescriptor < 0 || static_cast<uint64_t>(DirectoryFileDescriptor) >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* ExistingFile = CurrentProcess->FileTable[static_cast<size_t>(DirectoryFileDescriptor)];
            if (ExistingFile == nullptr || ExistingFile->Node == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            NodeDentry = ExistingFile->DirectoryEntry;
            if (NodeDentry == nullptr)
            {
                return LINUX_ERR_EFAULT;
            }
        }
    }
    else
    {
        if (KernelPath[0] == '\0')
        {
            return LINUX_ERR_ENOENT;
        }

        if (KernelPath[0] == '/')
        {
            NodeDentry = ((Flags & LINUX_AT_SYMLINK_NOFOLLOW) != 0) ? VFS->LookupNoFollowFinal(KernelPath) : VFS->Lookup(KernelPath);
        }
        else
        {
            Dentry* BaseDirectory = nullptr;

            if (DirectoryFileDescriptor == LINUX_AT_FDCWD)
            {
                BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
            }
            else
            {
                if (DirectoryFileDescriptor < 0 || static_cast<uint64_t>(DirectoryFileDescriptor) >= MAX_OPEN_FILES_PER_PROCESS)
                {
                    return LINUX_ERR_EBADF;
                }

                File* DirectoryFile = CurrentProcess->FileTable[static_cast<size_t>(DirectoryFileDescriptor)];
                if (DirectoryFile == nullptr || DirectoryFile->Node == nullptr)
                {
                    return LINUX_ERR_EBADF;
                }

                if (DirectoryFile->Node->NodeType != INODE_DIR)
                {
                    return LINUX_ERR_ENOTDIR;
                }

                BaseDirectory = DirectoryFile->DirectoryEntry;
            }

            if (BaseDirectory == nullptr)
            {
                return LINUX_ERR_ENOENT;
            }

            char BasePath[SYSCALL_PATH_MAX] = {};
            if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
            {
                return LINUX_ERR_EFAULT;
            }

            char     AbsolutePath[SYSCALL_PATH_MAX] = {};
            uint64_t BasePathLength                 = CStrLength(BasePath);
            uint64_t RelativeLength                 = CStrLength(KernelPath);
            uint64_t NeedsSeparator                 = (BasePathLength > 1) ? 1 : 0;
            uint64_t RequiredBytes                  = BasePathLength + NeedsSeparator + RelativeLength + 1;

            if (RequiredBytes > sizeof(AbsolutePath))
            {
                return LINUX_ERR_EINVAL;
            }

            memcpy(AbsolutePath, BasePath, static_cast<size_t>(BasePathLength));
            uint64_t Cursor = BasePathLength;
            if (NeedsSeparator != 0)
            {
                AbsolutePath[Cursor++] = '/';
            }

            memcpy(AbsolutePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
            AbsolutePath[Cursor + RelativeLength] = '\0';

            NodeDentry = ((Flags & LINUX_AT_SYMLINK_NOFOLLOW) != 0) ? VFS->LookupNoFollowFinal(AbsolutePath) : VFS->Lookup(AbsolutePath);
        }
    }

    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    LinuxStat KernelStat = {};
    PopulateLinuxStatFromNode(NodeDentry->inode, &KernelStat);
    if (!Logic->CopyFromKernelToUser(&KernelStat, Buffer, sizeof(KernelStat)))
    {
        return LINUX_ERR_EFAULT;
    }

    return 0;
}

int64_t TranslationLayer::HandleGetdents64SystemCall(uint64_t FileDescriptor, void* Buffer, uint64_t BufferSize)
{
    if (Logic == nullptr || (Buffer == nullptr && BufferSize != 0))
    {
        return LINUX_ERR_EFAULT;
    }

    if (BufferSize == 0)
    {
        return 0;
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

    if (OpenFile->Node->NodeType != INODE_DIR || OpenFile->DirectoryEntry == nullptr)
    {
        return LINUX_ERR_ENOTDIR;
    }

    uint8_t* KernelBuffer = reinterpret_cast<uint8_t*>(Logic->kmalloc(BufferSize));
    if (KernelBuffer == nullptr)
    {
        return LINUX_ERR_ENOMEM;
    }

    uint64_t Cursor          = 0;
    uint64_t EntryIndex      = OpenFile->CurrentOffset;
    Dentry*  DirectoryDentry = OpenFile->DirectoryEntry;
    uint64_t TotalEntries    = DirectoryDentry->child_count + 2;

    while (EntryIndex < TotalEntries)
    {
        const char* EntryName = nullptr;
        INode*      EntryNode = nullptr;

        if (EntryIndex == 0)
        {
            EntryName = ".";
            EntryNode = DirectoryDentry->inode;
        }
        else if (EntryIndex == 1)
        {
            EntryName = "..";
            EntryNode = (DirectoryDentry->parent != nullptr && DirectoryDentry->parent->inode != nullptr) ? DirectoryDentry->parent->inode : DirectoryDentry->inode;
        }
        else
        {
            uint64_t ChildIndex = EntryIndex - 2;
            Dentry*  Child      = DirectoryDentry->children[ChildIndex];
            if (Child == nullptr || Child->inode == nullptr || Child->name == nullptr)
            {
                ++EntryIndex;
                continue;
            }

            EntryName = Child->name;
            EntryNode = Child->inode;
        }

        uint64_t NameLength    = CStrLength(EntryName);
        uint64_t MinimumRecord = sizeof(LinuxDirent64Header) + NameLength + 1;
        uint64_t RecordLength  = AlignUpValue(MinimumRecord, 8);

        if (RecordLength > BufferSize)
        {
            Logic->kfree(KernelBuffer);
            return LINUX_ERR_EINVAL;
        }

        if ((Cursor + RecordLength) > BufferSize)
        {
            break;
        }

        LinuxDirent64Header Header = {};
        Header.Inode               = reinterpret_cast<uint64_t>(EntryNode);
        Header.Offset              = EntryIndex + 1;
        Header.RecordLength        = static_cast<uint16_t>(RecordLength);
        Header.Type                = BuildLinuxDirentTypeFromNode(EntryNode);

        memcpy(KernelBuffer + Cursor, &Header, sizeof(Header));
        memcpy(KernelBuffer + Cursor + sizeof(Header), EntryName, static_cast<size_t>(NameLength));
        KernelBuffer[Cursor + sizeof(Header) + NameLength] = '\0';

        uint64_t PaddingStart = Cursor + sizeof(Header) + NameLength + 1;
        for (uint64_t PaddingIndex = PaddingStart; PaddingIndex < (Cursor + RecordLength); ++PaddingIndex)
        {
            KernelBuffer[PaddingIndex] = 0;
        }

        Cursor += RecordLength;
        ++EntryIndex;
    }

    if (Cursor == 0)
    {
        Logic->kfree(KernelBuffer);
        return 0;
    }

    if (!Logic->CopyFromKernelToUser(KernelBuffer, Buffer, Cursor))
    {
        Logic->kfree(KernelBuffer);
        return LINUX_ERR_EFAULT;
    }

    OpenFile->CurrentOffset = EntryIndex;

    Logic->kfree(KernelBuffer);
    return static_cast<int64_t>(Cursor);
}

int64_t TranslationLayer::HandleFcntlSystemCall(uint64_t FileDescriptor, uint64_t Command, uint64_t Argument)
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

    auto DuplicateFromMinimum = [&](uint64_t MinimumFileDescriptor, bool SetCloseOnExec) -> int64_t
    {
        if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
        {
            return LINUX_ERR_EBADF;
        }

        File* SourceFile = CurrentProcess->FileTable[FileDescriptor];
        if (SourceFile == nullptr)
        {
            return LINUX_ERR_EBADF;
        }

        if (MinimumFileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
        {
            return LINUX_ERR_EINVAL;
        }

        for (size_t CandidateDescriptor = static_cast<size_t>(MinimumFileDescriptor); CandidateDescriptor < MAX_OPEN_FILES_PER_PROCESS; ++CandidateDescriptor)
        {
            if (CurrentProcess->FileTable[CandidateDescriptor] != nullptr)
            {
                continue;
            }

            File* DuplicatedFile = new File;
            if (DuplicatedFile == nullptr)
            {
                return LINUX_ERR_ENOMEM;
            }

            *DuplicatedFile                 = *SourceFile;
            DuplicatedFile->FileDescriptor  = CandidateDescriptor;
            DuplicatedFile->DescriptorFlags = SetCloseOnExec ? LINUX_FD_CLOEXEC : 0;

            CurrentProcess->FileTable[CandidateDescriptor] = DuplicatedFile;
            return static_cast<int64_t>(CandidateDescriptor);
        }

        return LINUX_ERR_EMFILE;
    };

    switch (Command)
    {
        case LINUX_F_DUPFD:
            return DuplicateFromMinimum(Argument, false);
        case LINUX_F_DUPFD_CLOEXEC:
            return DuplicateFromMinimum(Argument, true);
        case LINUX_F_GETFD:
        {
            if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
            if (OpenFile == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            return static_cast<int64_t>(OpenFile->DescriptorFlags & LINUX_FD_CLOEXEC);
        }
        case LINUX_F_SETFD:
        {
            if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
            if (OpenFile == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            OpenFile->DescriptorFlags = (Argument & LINUX_FD_CLOEXEC);
            return 0;
        }
        case LINUX_F_GETFL:
        {
            if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
            if (OpenFile == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            return static_cast<int64_t>(OpenFile->OpenFlags);
        }
        case LINUX_F_SETFL:
        {
            if (FileDescriptor >= MAX_OPEN_FILES_PER_PROCESS)
            {
                return LINUX_ERR_EBADF;
            }

            File* OpenFile = CurrentProcess->FileTable[FileDescriptor];
            if (OpenFile == nullptr)
            {
                return LINUX_ERR_EBADF;
            }

            uint64_t PreservedFlags = OpenFile->OpenFlags & ~LINUX_FCNTL_SETFL_ALLOWED;
            uint64_t RequestedFlags = Argument & LINUX_FCNTL_SETFL_ALLOWED;
            OpenFile->OpenFlags     = (PreservedFlags | RequestedFlags);
            return 0;
        }
        default:
            return LINUX_ERR_ENOSYS;
    }
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

int64_t TranslationLayer::HandleGetcwdSystemCall(char* Buffer, uint64_t Size)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Buffer == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Size == 0)
    {
        return LINUX_ERR_EINVAL;
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

    if (CurrentProcess->CurrentFileSystemLocation == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    char KernelPathBuffer[SYSCALL_PATH_MAX] = {};
    if (!BuildAbsolutePathFromDentry(CurrentProcess->CurrentFileSystemLocation, KernelPathBuffer, sizeof(KernelPathBuffer)))
    {
        return LINUX_ERR_ENOENT;
    }

    uint64_t PathBytesWithNull = CStrLength(KernelPathBuffer) + 1;
    if (PathBytesWithNull > Size)
    {
        return LINUX_ERR_ERANGE;
    }

    if (!Logic->CopyFromKernelToUser(KernelPathBuffer, Buffer, PathBytesWithNull))
    {
        return LINUX_ERR_EFAULT;
    }

    return reinterpret_cast<int64_t>(Buffer);
}

int64_t TranslationLayer::HandleChdirSystemCall(const char* Path)
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

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    char KernelPath[SYSCALL_PATH_MAX] = {};
    if (!CopyUserCString(Logic, Path, KernelPath, sizeof(KernelPath)))
    {
        return LINUX_ERR_EFAULT;
    }

    if (KernelPath[0] == '\0')
    {
        return LINUX_ERR_ENOENT;
    }

    char        EffectivePath[SYSCALL_PATH_MAX] = {};
    const char* LookupPath                      = KernelPath;

    if (KernelPath[0] != '/')
    {
        Dentry* BaseDirectory = CurrentProcess->CurrentFileSystemLocation;
        if (BaseDirectory == nullptr)
        {
            return LINUX_ERR_ENOENT;
        }

        char BasePath[SYSCALL_PATH_MAX] = {};
        if (!BuildAbsolutePathFromDentry(BaseDirectory, BasePath, sizeof(BasePath)))
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t BasePathLength = CStrLength(BasePath);
        uint64_t RelativeLength = CStrLength(KernelPath);
        uint64_t NeedsSeparator = (BasePathLength > 1) ? 1 : 0;
        uint64_t RequiredBytes  = BasePathLength + NeedsSeparator + RelativeLength + 1;
        if (RequiredBytes > sizeof(EffectivePath))
        {
            return LINUX_ERR_EINVAL;
        }

        memcpy(EffectivePath, BasePath, static_cast<size_t>(BasePathLength));
        uint64_t Cursor = BasePathLength;
        if (NeedsSeparator != 0)
        {
            EffectivePath[Cursor++] = '/';
        }

        memcpy(EffectivePath + Cursor, KernelPath, static_cast<size_t>(RelativeLength));
        EffectivePath[Cursor + RelativeLength] = '\0';
        LookupPath                             = EffectivePath;
    }

    Dentry* NodeDentry = VFS->Lookup(LookupPath);
    if (NodeDentry == nullptr || NodeDentry->inode == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    if (NodeDentry->inode->NodeType != INODE_DIR)
    {
        return LINUX_ERR_ENOTDIR;
    }

    CurrentProcess->CurrentFileSystemLocation = NodeDentry;
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

    uint64_t NewHeapPages    = NewHeapSize / PAGE_SIZE;
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

int64_t TranslationLayer::HandleGetpidSystemCall()
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

    return static_cast<int64_t>(CurrentProcess->Id);
}

int64_t TranslationLayer::HandleGetppidSystemCall()
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

    if (CurrentProcess->ParrentId == PROCESS_ID_INVALID)
    {
        return 0;
    }

    return static_cast<int64_t>(CurrentProcess->ParrentId);
}

int64_t TranslationLayer::HandleGetuidSystemCall()
{
    return 0;
}

int64_t TranslationLayer::HandleGetgidSystemCall()
{
    return 0;
}

int64_t TranslationLayer::HandleGeteuidSystemCall()
{
    return 0;
}

int64_t TranslationLayer::HandleGetegidSystemCall()
{
    return 0;
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

    *DuplicatedFile                              = *SourceFile;
    DuplicatedFile->FileDescriptor               = NewFileDescriptor;
    DuplicatedFile->DescriptorFlags              = 0;
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

    CurrentProcess->UserFSBase = GetUserFSBase();

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
    ChildProcess->State.rax    = 0;

    return static_cast<int64_t>(ChildId);
}

int64_t TranslationLayer::HandleVforkSystemCall()
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

    Process* ParentProcess = PM->GetRunningProcess();
    if (ParentProcess == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (ParentProcess->WaitingForVforkChild)
    {
        return LINUX_ERR_EAGAIN;
    }

    if (!ParentProcess->HasSavedSystemCallFrame || ParentProcess->AddressSpace == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ParentProcess->UserFSBase = GetUserFSBase();

    const ProcessSavedSystemCallFrame& SavedFrame = ParentProcess->SavedSystemCallFrame;

    CpuState ChildState = {};
    ChildState.rax      = 0;
    ChildState.rcx      = 0;
    ChildState.rdx      = SavedFrame.UserRDX;
    ChildState.rbx      = SavedFrame.UserRBX;
    ChildState.rbp      = SavedFrame.UserRBP;
    ChildState.rsi      = SavedFrame.UserRSI;
    ChildState.rdi      = SavedFrame.UserRDI;
    ChildState.r8       = SavedFrame.UserR8;
    ChildState.r9       = SavedFrame.UserR9;
    ChildState.r10      = SavedFrame.UserR10;
    ChildState.r11      = 0;
    ChildState.r12      = SavedFrame.UserR12;
    ChildState.r13      = SavedFrame.UserR13;
    ChildState.r14      = SavedFrame.UserR14;
    ChildState.r15      = SavedFrame.UserR15;
    ChildState.rip      = SavedFrame.UserRIP;
    ChildState.rflags   = SavedFrame.UserRFLAGS;
    ChildState.rsp      = SavedFrame.UserRSP;
    ChildState.cs       = USER_CS;
    ChildState.ss       = USER_SS;

    uint8_t ChildId = PM->CreateUserProcess(reinterpret_cast<void*>(ParentProcess->AddressSpace->GetStackVirtualAddressStart()), ChildState, ParentProcess->AddressSpace, ParentProcess->FileType);
    if (ChildId == PROCESS_ID_INVALID)
    {
        return LINUX_ERR_EAGAIN;
    }

    Process* ChildProcess = PM->GetProcessById(ChildId);
    if (ChildProcess == nullptr)
    {
        PM->KillProcess(ChildId);
        return LINUX_ERR_EFAULT;
    }

    ChildProcess->ParrentId                 = ParentProcess->Id;
    ChildProcess->UserFSBase                = ParentProcess->UserFSBase;
    ChildProcess->BlockedSignalMask         = ParentProcess->BlockedSignalMask;
    ChildProcess->ClearChildTidAddress      = ParentProcess->ClearChildTidAddress;
    ChildProcess->ProgramBreak              = ParentProcess->ProgramBreak;
    ChildProcess->CurrentFileSystemLocation = ParentProcess->CurrentFileSystemLocation;

    for (size_t SignalIndex = 0; SignalIndex < MAX_POSIX_SIGNALS_PER_PROCESS; ++SignalIndex)
    {
        ChildProcess->SignalActions[SignalIndex] = ParentProcess->SignalActions[SignalIndex];
    }

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        ChildProcess->MemoryMappings[MappingIndex] = ParentProcess->MemoryMappings[MappingIndex];
    }

    for (size_t FileIndex = 0; FileIndex < MAX_OPEN_FILES_PER_PROCESS; ++FileIndex)
    {
        if (ParentProcess->FileTable[FileIndex] == nullptr)
        {
            continue;
        }

        File* CopiedFile = new File;
        if (CopiedFile == nullptr)
        {
            PM->KillProcess(ChildId);
            return LINUX_ERR_ENOMEM;
        }

        *CopiedFile                        = *ParentProcess->FileTable[FileIndex];
        ChildProcess->FileTable[FileIndex] = CopiedFile;
    }

    ParentProcess->WaitingForVforkChild = true;
    ParentProcess->VforkChildId         = ChildId;

    ChildProcess->IsVforkChild  = true;
    ChildProcess->VforkParentId = ParentProcess->Id;

    Logic->AddProcessToReadyQueue(ChildId);

    if (ParentProcess->WaitingForSystemCallReturn && ParentProcess->HasSavedSystemCallFrame)
    {
        ParentProcess->State.cs = KERNEL_CS;
        ParentProcess->State.ss = KERNEL_SS;
    }

    Logic->BlockProcess(ParentProcess->Id);

    return static_cast<int64_t>(ChildId);
}

int64_t TranslationLayer::HandleExitGroupSystemCall(int64_t Status)
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

    bool    ExitingVforkChild = CurrentProcess->IsVforkChild;
    uint8_t VforkParentId     = CurrentProcess->VforkParentId;

    int32_t WaitStatus       = static_cast<int32_t>((static_cast<uint64_t>(Status) & 0xFFULL) << 8);
    uint8_t CurrentProcessId = CurrentProcess->Id;

    Logic->KillProcess(CurrentProcessId, WaitStatus);

    if (ExitingVforkChild)
    {
        Process* ParentProcess = PM->GetProcessById(VforkParentId);
        if (ParentProcess != nullptr && ParentProcess->Status != PROCESS_TERMINATED)
        {
            bool ShouldUnblockParent            = ParentProcess->WaitingForVforkChild && ParentProcess->VforkChildId == CurrentProcessId;
            ParentProcess->WaitingForVforkChild = false;
            ParentProcess->VforkChildId         = PROCESS_ID_INVALID;
            if (ShouldUnblockParent)
            {
                Logic->UnblockProcess(ParentProcess->Id);
            }
        }
    }

    Logic->Schedule();

    return 0;
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

#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr)
        {
            TTY* Terminal = ActiveDispatcher->GetResourceLayer()->GetTTY();
            if (Terminal != nullptr)
            {
                const char* Arg0 = (KernelArgv != nullptr && KernelArgc > 0 && KernelArgv[0] != nullptr) ? KernelArgv[0] : "<null>";
                const char* Arg1 = (KernelArgv != nullptr && KernelArgc > 1 && KernelArgv[1] != nullptr) ? KernelArgv[1] : "<null>";
                Terminal->printf_("execve_dbg: path='%s' argc=%lu argv0='%s' argv1='%s'\n", KernelPathBuffer, KernelArgc, Arg0, Arg1);
            }
        }
    }
#endif

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

    for (size_t FileIndex = 0; FileIndex < MAX_OPEN_FILES_PER_PROCESS; ++FileIndex)
    {
        File* OpenFile = CurrentProcess->FileTable[FileIndex];
        if (OpenFile == nullptr)
        {
            continue;
        }

        if ((OpenFile->DescriptorFlags & LINUX_FD_CLOEXEC) == 0)
        {
            continue;
        }

        delete OpenFile;
        CurrentProcess->FileTable[FileIndex] = nullptr;
    }

    ReleaseVforkParentIfNeeded(Logic, CurrentProcess);

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

        PhysicalMemoryManager* PMM                = ActiveDispatcher->GetResourceLayer()->GetPMM();
        uint64_t               PageCount          = MappingLength / PAGE_SIZE;
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
        uint64_t UserPageTable   = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
        uint64_t ActivePageTable = ActiveDispatcher->GetResourceLayer()->ReadCurrentPageTable();
        if (UserPageTable != 0 && ActivePageTable == UserPageTable)
        {
            ActiveDispatcher->GetResourceLayer()->LoadPageTable(ActivePageTable);
        }
    }

    return static_cast<int64_t>(FileMappedAddress);
}

int64_t TranslationLayer::HandleMunmapSystemCall(void* Address, uint64_t Length)
{
    if (Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Length == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t UnmapStart = reinterpret_cast<uint64_t>(Address);
    if ((UnmapStart & (PAGE_SIZE - 1)) != 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t UnmapLength = AlignUpToPageBoundary(Length);
    if (UnmapLength == 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t UnmapEnd = UnmapStart + UnmapLength;
    if (UnmapEnd < UnmapStart)
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

    size_t ResultingInUseMappings = 0;

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        const ProcessMemoryMapping& Mapping = CurrentProcess->MemoryMappings[MappingIndex];
        if (!Mapping.InUse)
        {
            continue;
        }

        uint64_t MappingStart = Mapping.VirtualAddressStart;
        uint64_t MappingEnd   = MappingStart + Mapping.Length;
        if (MappingEnd <= MappingStart)
        {
            continue;
        }

        uint64_t OverlapStart = (UnmapStart > MappingStart) ? UnmapStart : MappingStart;
        uint64_t OverlapEnd   = (UnmapEnd < MappingEnd) ? UnmapEnd : MappingEnd;

        if (OverlapStart >= OverlapEnd)
        {
            ++ResultingInUseMappings;
            continue;
        }

        bool TrimsLeft  = (OverlapStart > MappingStart);
        bool TrimsRight = (OverlapEnd < MappingEnd);

        if (TrimsLeft && TrimsRight)
        {
            ResultingInUseMappings += 2;
        }
        else if (TrimsLeft || TrimsRight)
        {
            ResultingInUseMappings += 1;
        }
    }

    if (ResultingInUseMappings > MAX_MEMORY_MAPPINGS_PER_PROCESS)
    {
        return LINUX_ERR_ENOMEM;
    }

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr || ActiveDispatcher->GetResourceLayer() == nullptr || ActiveDispatcher->GetResourceLayer()->GetPMM() == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    ResourceLayer*         Resource = ActiveDispatcher->GetResourceLayer();
    PhysicalMemoryManager* PMM      = Resource->GetPMM();

    uint64_t UserPageTable = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
    if (UserPageTable == 0)
    {
        return LINUX_ERR_EFAULT;
    }

    VirtualMemoryManager UserVMM(UserPageTable, *PMM);

    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        ProcessMemoryMapping& Mapping = CurrentProcess->MemoryMappings[MappingIndex];
        if (!Mapping.InUse)
        {
            continue;
        }

        ProcessMemoryMapping OriginalMapping = Mapping;

        uint64_t MappingStart = OriginalMapping.VirtualAddressStart;
        uint64_t MappingEnd   = MappingStart + OriginalMapping.Length;
        if (MappingEnd <= MappingStart)
        {
            continue;
        }

        uint64_t OverlapStart = (UnmapStart > MappingStart) ? UnmapStart : MappingStart;
        uint64_t OverlapEnd   = (UnmapEnd < MappingEnd) ? UnmapEnd : MappingEnd;

        if (OverlapStart >= OverlapEnd)
        {
            continue;
        }

        uint64_t OverlapLength = OverlapEnd - OverlapStart;

        if (OriginalMapping.IsAnonymous && OriginalMapping.PhysicalAddressStart != 0)
        {
            uint64_t PhysicalOverlapStart = OriginalMapping.PhysicalAddressStart + (OverlapStart - MappingStart);
            uint64_t OverlapPages         = OverlapLength / PAGE_SIZE;
            if (OverlapPages != 0)
            {
                PMM->FreePagesFromDescriptor(reinterpret_cast<void*>(PhysicalOverlapStart), OverlapPages);
            }
        }

        for (uint64_t VirtualAddress = OverlapStart; VirtualAddress < OverlapEnd; VirtualAddress += PAGE_SIZE)
        {
            UserVMM.UnmapPage(VirtualAddress);
        }

        bool TrimsLeft  = (OverlapStart > MappingStart);
        bool TrimsRight = (OverlapEnd < MappingEnd);

        if (!TrimsLeft && !TrimsRight)
        {
            Mapping = {};
            continue;
        }

        if (!TrimsLeft && TrimsRight)
        {
            uint64_t NewLength          = MappingEnd - OverlapEnd;
            Mapping.VirtualAddressStart = OverlapEnd;
            Mapping.Length              = NewLength;
            if (Mapping.PhysicalAddressStart != 0)
            {
                Mapping.PhysicalAddressStart = OriginalMapping.PhysicalAddressStart + (OverlapEnd - MappingStart);
            }
            continue;
        }

        if (TrimsLeft && !TrimsRight)
        {
            Mapping.Length = OverlapStart - MappingStart;
            continue;
        }

        int64_t FreeSlot = FindAvailableMappingSlot(CurrentProcess);
        if (FreeSlot < 0)
        {
            return LINUX_ERR_ENOMEM;
        }

        ProcessMemoryMapping& RightMapping = CurrentProcess->MemoryMappings[static_cast<size_t>(FreeSlot)];
        RightMapping                       = OriginalMapping;
        RightMapping.InUse                 = true;
        RightMapping.VirtualAddressStart   = OverlapEnd;
        RightMapping.Length                = MappingEnd - OverlapEnd;
        if (RightMapping.PhysicalAddressStart != 0)
        {
            RightMapping.PhysicalAddressStart = OriginalMapping.PhysicalAddressStart + (OverlapEnd - MappingStart);
        }

        Mapping.VirtualAddressStart = MappingStart;
        Mapping.Length              = OverlapStart - MappingStart;
    }

    uint64_t ActivePageTable = Resource->ReadCurrentPageTable();
    if (ActivePageTable == UserPageTable)
    {
        Resource->LoadPageTable(ActivePageTable);
    }

    return 0;
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

    if (Code == LINUX_ARCH_SET_FS)
    {
        if (!IsCanonicalX86_64Address(Address))
        {
            return LINUX_ERR_EPERM;
        }

        CurrentProcess->UserFSBase = Address;
        SetUserFSBase(Address);
        return 0;
    }

    if (Code == LINUX_ARCH_GET_FS)
    {
        if (Address == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        uint64_t CurrentFSBase = CurrentProcess->UserFSBase;
        if (!Logic->CopyFromKernelToUser(&CurrentFSBase, reinterpret_cast<void*>(Address), sizeof(CurrentFSBase)))
        {
            return LINUX_ERR_EFAULT;
        }

        return 0;
    }

    return LINUX_ERR_EINVAL;
}

int64_t TranslationLayer::HandleRtSigactionSystemCall(int64_t Signal, const void* Action, void* OldAction, uint64_t SigsetSize)
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

    if (SigsetSize != LINUX_RT_SIGSET_SIZE)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Signal < LINUX_SIGNAL_MIN || Signal > LINUX_SIGNAL_MAX)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Signal == LINUX_SIGNAL_SIGKILL || Signal == LINUX_SIGNAL_SIGSTOP)
    {
        return LINUX_ERR_EINVAL;
    }

    size_t SignalIndex = static_cast<size_t>(Signal - 1);

    LinuxKernelSigAction OldKernelAction = {};
    OldKernelAction.Handler              = CurrentProcess->SignalActions[SignalIndex].Handler;
    OldKernelAction.Flags                = CurrentProcess->SignalActions[SignalIndex].Flags;
    OldKernelAction.Restorer             = CurrentProcess->SignalActions[SignalIndex].Restorer;
    OldKernelAction.Mask                 = CurrentProcess->SignalActions[SignalIndex].Mask;

    if (OldAction != nullptr)
    {
        if (!Logic->CopyFromKernelToUser(&OldKernelAction, OldAction, LINUX_RT_SIGACTION_SIZE))
        {
            return LINUX_ERR_EFAULT;
        }
    }

    if (Action != nullptr)
    {
        LinuxKernelSigAction NewKernelAction = {};
        if (!Logic->CopyFromUserToKernel(Action, &NewKernelAction, LINUX_RT_SIGACTION_SIZE))
        {
            return LINUX_ERR_EFAULT;
        }

        NewKernelAction.Mask &= ~LINUX_UNBLOCKABLE_SIGNAL_MASK;

        CurrentProcess->SignalActions[SignalIndex].Handler  = NewKernelAction.Handler;
        CurrentProcess->SignalActions[SignalIndex].Flags    = NewKernelAction.Flags;
        CurrentProcess->SignalActions[SignalIndex].Restorer = NewKernelAction.Restorer;
        CurrentProcess->SignalActions[SignalIndex].Mask     = NewKernelAction.Mask;

        if (CurrentProcess->SignalActions[SignalIndex].Handler == static_cast<uint64_t>(LINUX_SIG_DFL) || CurrentProcess->SignalActions[SignalIndex].Handler == static_cast<uint64_t>(LINUX_SIG_IGN))
        {
            return 0;
        }
    }

    return 0;
}

int64_t TranslationLayer::HandleRtSigprocmaskSystemCall(int64_t How, const void* Set, void* OldSet, uint64_t SigsetSize)
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

    if (SigsetSize != LINUX_RT_SIGSET_SIZE)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t RequestedMask    = 0;
    bool     ShouldUpdateMask = (Set != nullptr);
    if (ShouldUpdateMask)
    {
        if (!Logic->CopyFromUserToKernel(Set, &RequestedMask, sizeof(RequestedMask)))
        {
            return LINUX_ERR_EFAULT;
        }

        RequestedMask &= ~LINUX_UNBLOCKABLE_SIGNAL_MASK;

        if (How == LINUX_SIG_BLOCK)
        {
            RequestedMask |= CurrentProcess->BlockedSignalMask;
        }
        else if (How == LINUX_SIG_UNBLOCK)
        {
            RequestedMask = CurrentProcess->BlockedSignalMask & ~RequestedMask;
        }
        else if (How != LINUX_SIG_SETMASK)
        {
            return LINUX_ERR_EINVAL;
        }
    }

    uint64_t PreviousMask = CurrentProcess->BlockedSignalMask;
    if (ShouldUpdateMask)
    {
        CurrentProcess->BlockedSignalMask = RequestedMask;
    }

    if (OldSet != nullptr && !Logic->CopyFromKernelToUser(&PreviousMask, OldSet, sizeof(PreviousMask)))
    {
        return LINUX_ERR_EFAULT;
    }

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