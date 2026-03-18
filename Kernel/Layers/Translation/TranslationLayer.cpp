/**
 * File: TranslationLayer.cpp
 * Author: Marwan Mostafa
 * Description: Translation layer implementation between system layers.
 */

#include "TranslationLayer.hpp"

#include "Layers/Logic/LogicLayer.hpp"

namespace
{
constexpr int64_t LINUX_ERR_EFAULT = -14;
constexpr int64_t LINUX_ERR_ENOENT = -2;
constexpr int64_t LINUX_ERR_ENOMEM = -12;
constexpr int64_t LINUX_ERR_EMFILE = -24;
constexpr int64_t LINUX_ERR_EINVAL = -22;
constexpr int64_t LINUX_ERR_EBADF  = -9;
constexpr int64_t LINUX_ERR_ENOSYS = -38;

constexpr uint64_t SYSCALL_COPY_CHUNK_SIZE = 4096;
constexpr uint64_t SYSCALL_PATH_MAX         = 4096;

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
        char Character = 0;
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

    uint8_t KernelBuffer[SYSCALL_COPY_CHUNK_SIZE];
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

    uint8_t KernelBuffer[SYSCALL_COPY_CHUNK_SIZE];
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

int64_t TranslationLayer::HandleExecveSystemCall(const char* Path, const char* const* Argv, const char* const* Envp)
{
    (void) Argv;
    (void) Envp;

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

    uint8_t ChangedProcessId = Logic->ChangeProcessExecution(CurrentProcess->Id, KernelPathBuffer);
    Logic->kfree(KernelPathBuffer);

    if (ChangedProcessId == PROCESS_ID_INVALID)
    {
        return LINUX_ERR_ENOENT;
    }

    return 0;
}