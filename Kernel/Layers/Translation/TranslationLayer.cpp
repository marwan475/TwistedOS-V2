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
constexpr int64_t LINUX_ERR_EMFILE = -24;
constexpr int64_t LINUX_ERR_EINVAL = -22;
constexpr int64_t LINUX_ERR_EBADF  = -9;

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