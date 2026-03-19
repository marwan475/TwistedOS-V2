/**
 * File: TranslationLayer.cpp
 * Author: Marwan Mostafa
 * Description: Translation layer implementation between system layers.
 */

#include "TranslationLayer.hpp"

#include "Layers/Logic/LogicLayer.hpp"

#include <CommonUtils.hpp>

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

constexpr uint64_t SYSCALL_COPY_CHUNK_SIZE = 4096;
constexpr uint64_t SYSCALL_PATH_MAX        = 4096;
constexpr uint64_t SYSCALL_EXEC_MAX_VECTOR = 128;

constexpr uint64_t LINUX_O_ACCMODE = 0x3;

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