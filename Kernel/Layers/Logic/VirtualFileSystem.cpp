/**
 * File: VirtualFileSystem.cpp
 * Author: Marwan Mostafa
 * Description: Virtual file system abstraction layer implementation.
 */

#include "VirtualFileSystem.hpp"

#include "../Dispatcher.hpp"
#include "../Resource/ExtendedFileSystemManager.hpp"
#include "../Resource/RamFileSystemManager.hpp"
#include "../Resource/TTY.hpp"

#include <CommonUtils.hpp>

namespace
{
constexpr char     PATH_SEPARATOR           = '/';
constexpr char     PATH_DOT                 = '.';
constexpr char     STRING_TERMINATOR        = '\0';
constexpr char     DEV_DIRECTORY_NAME[]     = "dev";
constexpr uint64_t CHILDREN_GROWTH_ONE      = 1;
constexpr uint64_t ROOT_FILE_SIZE           = 0;
constexpr uint64_t ROOT_TREE_DEPTH          = 0;
constexpr uint64_t INDENT_SPACES_PER_LEVEL  = 2;
constexpr uint64_t MAX_SYMLINK_FOLLOW_DEPTH = 40;

constexpr int64_t LINUX_ERR_EFAULT = -14;
constexpr int64_t LINUX_ERR_EISDIR = -21;
constexpr int64_t LINUX_ERR_ENOSPC = -28;
constexpr int64_t LINUX_ERR_ENOSYS = -38;

typedef struct
{
    Dentry* RootDentry;
    bool    IsSuccessful;
} MountInitRamFileSystemContext;

typedef struct
{
    Dentry*     RootDentry;
    const char* MountPath;
    const void* FileSystemManager;
    bool        IsSuccessful;
} MountEXTFileSystemContext;

bool    IsRootAliasPath(const char* NormalizedPath);
Dentry* FindChildBySegment(Dentry* Parent, const char* SegmentStart, uint64_t SegmentLength);
bool    AppendChild(Dentry* Parent, Dentry* Child);
bool    RemoveChild(Dentry* Parent, Dentry* Child);
bool    IsDescendantDentry(const Dentry* CandidateDescendant, const Dentry* CandidateAncestor);
void    TransferDevDirectoryIfMissing(Dentry* OldRoot, Dentry* NewRoot);

bool EnsureLazyLoadedINodeData(INode* Node)
{
    if (Node == nullptr)
    {
        return false;
    }

    if (!Node->IsLazyLoad)
    {
        return Node->NodeData != nullptr;
    }

    if (Node->NodeData != nullptr)
    {
        return true;
    }

    if (Node->NodeSize == 0)
    {
        return true;
    }

    if (Node->BackingInodeNumber == 0 || Node->LazyLoadContext == nullptr)
    {
        return false;
    }

    ExtendedFileSystemManager* FileSystemManager = reinterpret_cast<ExtendedFileSystemManager*>(Node->LazyLoadContext);
    if (FileSystemManager == nullptr || !FileSystemManager->IsInitialized())
    {
        return false;
    }

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        return false;
    }

    ResourceLayer* Resource = ActiveDispatcher->GetResourceLayer();
    if (Resource == nullptr || Resource->GetPMM() == nullptr)
    {
        return false;
    }

    uint64_t PageCount = (Node->NodeSize + PAGE_SIZE - 1) / PAGE_SIZE;
    void*    FileData  = Resource->GetPMM()->AllocatePagesFromDescriptor(PageCount);
    if (FileData == nullptr)
    {
        return false;
    }

    kmemset(FileData, 0, static_cast<size_t>(PageCount * PAGE_SIZE));

    if (!FileSystemManager->LoadInodeData(Node->BackingInodeNumber, FileData, Node->NodeSize))
    {
        Resource->GetPMM()->FreePagesFromDescriptor(FileData, PageCount);
        return false;
    }

    Node->NodeData            = FileData;
    Node->LazyDataBackedByPMM = true;
    Node->LazyDataPageCount   = PageCount;
    return true;
}

bool EnsureWritableINodeCapacity(INode* Node, uint64_t RequiredSize)
{
    if (Node == nullptr)
    {
        return false;
    }

    if (RequiredSize == 0)
    {
        return true;
    }

    if (Node->NodeData != nullptr && Node->NodeSize >= RequiredSize)
    {
        return true;
    }

    if (Node->NodeData == nullptr && Node->IsLazyLoad && Node->NodeSize > 0)
    {
        if (!EnsureLazyLoadedINodeData(Node))
        {
            return false;
        }

        if (Node->NodeData != nullptr && Node->NodeSize >= RequiredSize)
        {
            return true;
        }
    }

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        return false;
    }

    ResourceLayer* Resource = ActiveDispatcher->GetResourceLayer();
    if (Resource == nullptr || Resource->GetPMM() == nullptr)
    {
        return false;
    }

    uint64_t ExistingSize = Node->NodeSize;
    uint64_t TargetSize   = (RequiredSize > ExistingSize) ? RequiredSize : ExistingSize;
    uint64_t PageCount    = (TargetSize + PAGE_SIZE - 1) / PAGE_SIZE;
    void*    NewData      = Resource->GetPMM()->AllocatePagesFromDescriptor(PageCount);
    if (NewData == nullptr)
    {
        return false;
    }

    kmemset(NewData, 0, static_cast<size_t>(PageCount * PAGE_SIZE));

    if (Node->NodeData != nullptr && ExistingSize > 0)
    {
        memcpy(NewData, Node->NodeData, static_cast<size_t>(ExistingSize));
    }

    if (Node->LazyDataBackedByPMM && Node->NodeData != nullptr && Node->LazyDataPageCount != 0)
    {
        Resource->GetPMM()->FreePagesFromDescriptor(Node->NodeData, Node->LazyDataPageCount);
    }

    Node->NodeData            = NewData;
    Node->NodeSize            = TargetSize;
    Node->IsLazyLoad          = false;
    Node->LazyDataBackedByPMM = true;
    Node->LazyDataPageCount   = PageCount;
    Node->BackingInodeNumber  = 0;
    Node->LazyLoadRefCount    = 0;
    Node->LazyLoadContext     = nullptr;

    return true;
}

int64_t DefaultReadFileOperation(File* OpenFile, void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr || (Buffer == nullptr && Count != 0))
    {
        return LINUX_ERR_EFAULT;
    }

    if (OpenFile->Node->NodeType == INODE_DIR)
    {
        return LINUX_ERR_EISDIR;
    }

    if (OpenFile->Node->NodeType != INODE_FILE)
    {
        return LINUX_ERR_ENOSYS;
    }

    if (Count == 0)
    {
        return 0;
    }

    if (OpenFile->Node->NodeData == nullptr)
    {
        if (OpenFile->Node->NodeSize == 0)
        {
            return 0;
        }

        if (!EnsureLazyLoadedINodeData(OpenFile->Node))
        {
            return LINUX_ERR_EFAULT;
        }

        if (OpenFile->Node->NodeData == nullptr)
        {
            return LINUX_ERR_EFAULT;
        }
    }

    uint64_t FileSize = OpenFile->Node->NodeSize;
    if (OpenFile->CurrentOffset >= FileSize)
    {
        return 0;
    }

    uint64_t RemainingBytes = FileSize - OpenFile->CurrentOffset;
    uint64_t BytesToRead    = (Count < RemainingBytes) ? Count : RemainingBytes;

    const uint8_t* Source = reinterpret_cast<const uint8_t*>(OpenFile->Node->NodeData) + OpenFile->CurrentOffset;
    memcpy(Buffer, Source, static_cast<size_t>(BytesToRead));
    OpenFile->CurrentOffset += BytesToRead;

    return static_cast<int64_t>(BytesToRead);
}

int64_t DefaultWriteFileOperation(File* OpenFile, const void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr || (Buffer == nullptr && Count != 0))
    {
        return LINUX_ERR_EFAULT;
    }

    if (OpenFile->Node->NodeType == INODE_DIR)
    {
        return LINUX_ERR_EISDIR;
    }

    if (OpenFile->Node->NodeType != INODE_FILE)
    {
        return LINUX_ERR_ENOSYS;
    }

    if (Count == 0)
    {
        return 0;
    }

    if (OpenFile->CurrentOffset > (UINT64_MAX - Count))
    {
        return LINUX_ERR_ENOSPC;
    }

    uint64_t RequiredSize = OpenFile->CurrentOffset + Count;
    if (!EnsureWritableINodeCapacity(OpenFile->Node, RequiredSize))
    {
        return LINUX_ERR_ENOSPC;
    }

    uint8_t* Destination = reinterpret_cast<uint8_t*>(OpenFile->Node->NodeData) + OpenFile->CurrentOffset;
    memcpy(Destination, Buffer, static_cast<size_t>(Count));
    OpenFile->CurrentOffset += Count;

    return static_cast<int64_t>(Count);
}

int64_t DefaultSeekFileOperation(File* OpenFile, int64_t Offset, int32_t Whence)
{
    (void) OpenFile;
    (void) Offset;
    (void) Whence;
    return LINUX_ERR_ENOSYS;
}

int64_t DefaultMemoryMapFileOperation(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address)
{
    (void) OpenFile;
    (void) Length;
    (void) Offset;
    (void) AddressSpace;
    (void) Address;
    return LINUX_ERR_ENOSYS;
}

int64_t DefaultIoctlFileOperation(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic, Process* RunningProcess)
{
    (void) OpenFile;
    (void) Request;
    (void) Argument;
    (void) Logic;
    (void) RunningProcess;
    return LINUX_ERR_ENOSYS;
}

int64_t DefaultPollFileOperation(File* OpenFile, uint32_t RequestedEvents, uint32_t* ReturnedEvents, LogicLayer* Logic, Process* RunningProcess)
{
    (void) OpenFile;
    (void) RequestedEvents;
    (void) ReturnedEvents;
    (void) Logic;
    (void) RunningProcess;
    return LINUX_ERR_ENOSYS;
}

FileOperations DefaultFileOperations = {
        &DefaultReadFileOperation, &DefaultWriteFileOperation, &DefaultSeekFileOperation, &DefaultMemoryMapFileOperation, &DefaultPollFileOperation,
        &DefaultIoctlFileOperation,
};

/**
 * Function: GetStringLength
 * Description: Returns the length of a null-terminated C string.
 * Parameters:
 *   const char* Text - Input string.
 * Returns:
 *   uint64_t - Number of characters before the null terminator.
 */
uint64_t GetStringLength(const char* Text)
{
    if (Text == nullptr)
    {
        return 0;
    }

    uint64_t Length = 0;
    while (Text[Length] != STRING_TERMINATOR)
    {
        ++Length;
    }

    return Length;
}

/**
 * Function: DuplicateString
 * Description: Allocates and copies a null-terminated C string.
 * Parameters:
 *   const char* Source - Source string to copy.
 * Returns:
 *   char* - Newly allocated copy, or nullptr when source is null.
 */
char* DuplicateString(const char* Source)
{
    if (Source == nullptr)
    {
        return nullptr;
    }

    uint64_t Length      = GetStringLength(Source);
    uint64_t BufferBytes = Length + CHILDREN_GROWTH_ONE;
    char*    Copy        = new char[BufferBytes];
    strcpy(Copy, Source);
    return Copy;
}

/**
 * Function: DuplicateSegment
 * Description: Allocates and copies a fixed-length path segment into a null-terminated string.
 * Parameters:
 *   const char* SegmentStart - Segment start pointer.
 *   uint64_t SegmentLength - Segment length in bytes.
 * Returns:
 *   char* - Newly allocated segment copy.
 */
char* DuplicateSegment(const char* SegmentStart, uint64_t SegmentLength)
{
    uint64_t BufferBytes = SegmentLength + CHILDREN_GROWTH_ONE;
    char*    Copy        = new char[BufferBytes];

    for (uint64_t Index = 0; Index < SegmentLength; ++Index)
    {
        Copy[Index] = SegmentStart[Index];
    }

    Copy[SegmentLength] = STRING_TERMINATOR;
    return Copy;
}

/**
 * Function: MatchSegmentName
 * Description: Compares a null-terminated name against a non-null-terminated segment.
 * Parameters:
 *   const char* Name - Null-terminated name.
 *   const char* SegmentStart - Segment start pointer.
 *   uint64_t SegmentLength - Segment length in bytes.
 * Returns:
 *   bool - True when names match exactly.
 */
bool MatchSegmentName(const char* Name, const char* SegmentStart, uint64_t SegmentLength)
{
    if (Name == nullptr || SegmentStart == nullptr)
    {
        return false;
    }

    for (uint64_t Index = 0; Index < SegmentLength; ++Index)
    {
        if (Name[Index] == STRING_TERMINATOR || Name[Index] != SegmentStart[Index])
        {
            return false;
        }
    }

    return Name[SegmentLength] == STRING_TERMINATOR;
}

/**
 * Function: NormalizePathStart
 * Description: Skips leading '/' and './' path prefixes.
 * Parameters:
 *   const char* Path - Input path string.
 * Returns:
 *   const char* - Pointer to normalized path start.
 */
const char* NormalizePathStart(const char* Path)
{
    if (Path == nullptr)
    {
        return nullptr;
    }

    while (*Path == PATH_SEPARATOR)
    {
        ++Path;
    }

    while (Path[0] == PATH_DOT && Path[1] == PATH_SEPARATOR)
    {
        Path += 2;
    }

    return Path;
}

/**
 * Function: DecodeNodeType
 * Description: Converts ram filesystem entry type into VFS file type.
 * Parameters:
 *   RamFileSystemEntryType EntryType - Ram filesystem entry type.
 * Returns:
 *   FileType - VFS file type.
 */
FileType DecodeNodeType(RamFileSystemEntryType EntryType)
{
    if (EntryType == RamFileSystemEntryTypeDirectory)
    {
        return INODE_DIR;
    }

    if (EntryType == RamFileSystemEntryTypeRegularFile)
    {
        return INODE_FILE;
    }

    if (EntryType == RamFileSystemEntryTypeSymbolicLink)
    {
        return INODE_SYMLINK;
    }

    return INODE_DEV;
}

FileType DecodeNodeType(ExtendedFileSystemEntryType EntryType)
{
    if (EntryType == ExtendedFileSystemEntryTypeDirectory)
    {
        return INODE_DIR;
    }

    if (EntryType == ExtendedFileSystemEntryTypeRegularFile)
    {
        return INODE_FILE;
    }

    if (EntryType == ExtendedFileSystemEntryTypeSymbolicLink)
    {
        return INODE_SYMLINK;
    }

    return INODE_FILE;
}

char* ComposeMountPath(const char* MountPath, const char* EntryPath)
{
    if (MountPath == nullptr || EntryPath == nullptr)
    {
        return nullptr;
    }

    const char* RelativePath = EntryPath;
    while (*RelativePath == PATH_SEPARATOR)
    {
        ++RelativePath;
    }

    uint64_t MountLength = GetStringLength(MountPath);
    uint64_t EntryLength = GetStringLength(RelativePath);

    bool MountIsRoot    = (MountLength == 1 && MountPath[0] == PATH_SEPARATOR);
    bool NeedsSeparator = (MountLength > 0 && !MountIsRoot && MountPath[MountLength - 1] != PATH_SEPARATOR && EntryLength > 0);

    uint64_t CombinedLength = MountLength + (NeedsSeparator ? 1 : 0) + EntryLength;
    char*    CombinedPath   = new char[CombinedLength + 1];

    uint64_t Cursor = 0;
    memcpy(CombinedPath + Cursor, MountPath, static_cast<size_t>(MountLength));
    Cursor += MountLength;

    if (NeedsSeparator)
    {
        CombinedPath[Cursor] = PATH_SEPARATOR;
        ++Cursor;
    }

    if (EntryLength > 0)
    {
        memcpy(CombinedPath + Cursor, RelativePath, static_cast<size_t>(EntryLength));
        Cursor += EntryLength;
    }

    CombinedPath[Cursor] = STRING_TERMINATOR;
    return CombinedPath;
}

bool IsDotSegment(const char* SegmentStart, uint64_t SegmentLength)
{
    return SegmentLength == 1 && SegmentStart[0] == PATH_DOT;
}

bool IsDotDotSegment(const char* SegmentStart, uint64_t SegmentLength)
{
    return SegmentLength == 2 && SegmentStart[0] == PATH_DOT && SegmentStart[1] == PATH_DOT;
}

Dentry* ResolvePathInternal(Dentry* RootDentry, Dentry* StartDentry, const char* Path, uint64_t RemainingSymlinkDepth, bool FollowFinalSymlink)
{
    if (RootDentry == nullptr || StartDentry == nullptr || Path == nullptr)
    {
        return nullptr;
    }

    const char* EffectivePath = Path;
    Dentry*     Current       = StartDentry;

    if (Path[0] == PATH_SEPARATOR)
    {
        Current       = RootDentry;
        EffectivePath = NormalizePathStart(Path);

        if (IsRootAliasPath(EffectivePath))
        {
            return RootDentry;
        }
    }
    else
    {
        while (EffectivePath[0] == PATH_DOT && EffectivePath[1] == PATH_SEPARATOR)
        {
            EffectivePath += 2;
        }

        if (EffectivePath[0] == STRING_TERMINATOR)
        {
            return Current;
        }
    }

    const char* SegmentStart = EffectivePath;
    while (*SegmentStart != STRING_TERMINATOR)
    {
        while (*SegmentStart == PATH_SEPARATOR)
        {
            ++SegmentStart;
        }

        if (*SegmentStart == STRING_TERMINATOR)
        {
            break;
        }

        const char* SegmentEnd = SegmentStart;
        while (*SegmentEnd != STRING_TERMINATOR && *SegmentEnd != PATH_SEPARATOR)
        {
            ++SegmentEnd;
        }

        uint64_t SegmentLength = static_cast<uint64_t>(SegmentEnd - SegmentStart);
        if (IsDotSegment(SegmentStart, SegmentLength))
        {
            SegmentStart = SegmentEnd;
            continue;
        }

        if (IsDotDotSegment(SegmentStart, SegmentLength))
        {
            if (Current->parent != nullptr)
            {
                Current = Current->parent;
            }

            SegmentStart = SegmentEnd;
            continue;
        }

        Dentry* Next = FindChildBySegment(Current, SegmentStart, SegmentLength);
        if (Next == nullptr || Next->inode == nullptr)
        {
            return nullptr;
        }

        bool IsFinalSegment = (*SegmentEnd == STRING_TERMINATOR);

        if (Next->inode->NodeType == INODE_SYMLINK)
        {
            if (!FollowFinalSymlink && IsFinalSegment)
            {
                Current      = Next;
                SegmentStart = SegmentEnd;
                continue;
            }

            if (RemainingSymlinkDepth == 0)
            {
                return nullptr;
            }

            if (Next->inode->NodeData == nullptr)
            {
                if (!EnsureLazyLoadedINodeData(Next->inode) || Next->inode->NodeData == nullptr)
                {
                    return nullptr;
                }
            }

            uint64_t TargetLength = Next->inode->NodeSize;
            char*    TargetPath   = new char[TargetLength + 1];
            memcpy(TargetPath, Next->inode->NodeData, static_cast<size_t>(TargetLength));
            TargetPath[TargetLength] = STRING_TERMINATOR;

            const char* RemainderPath = SegmentEnd;
            if (*RemainderPath == PATH_SEPARATOR)
            {
                ++RemainderPath;
            }

            uint64_t RemainderLength = GetStringLength(RemainderPath);
            bool     AppendSeparator = (RemainderLength > 0 && TargetLength > 0 && TargetPath[TargetLength - 1] != PATH_SEPARATOR);
            uint64_t CombinedLength  = TargetLength + (AppendSeparator ? 1 : 0) + RemainderLength;
            char*    CombinedPath    = new char[CombinedLength + 1];

            uint64_t Cursor = 0;
            if (TargetLength > 0)
            {
                memcpy(CombinedPath + Cursor, TargetPath, static_cast<size_t>(TargetLength));
                Cursor += TargetLength;
            }

            if (AppendSeparator)
            {
                CombinedPath[Cursor] = PATH_SEPARATOR;
                ++Cursor;
            }

            if (RemainderLength > 0)
            {
                memcpy(CombinedPath + Cursor, RemainderPath, static_cast<size_t>(RemainderLength));
                Cursor += RemainderLength;
            }

            CombinedPath[Cursor] = STRING_TERMINATOR;

            Dentry* SymlinkBase = (TargetLength > 0 && TargetPath[0] == PATH_SEPARATOR) ? RootDentry : ((Next->parent != nullptr) ? Next->parent : RootDentry);
            Dentry* Resolved    = ResolvePathInternal(RootDentry, SymlinkBase, CombinedPath, RemainingSymlinkDepth - 1, FollowFinalSymlink);

            delete[] CombinedPath;
            delete[] TargetPath;
            return Resolved;
        }

        Current      = Next;
        SegmentStart = SegmentEnd;
    }

    return Current;
}

/**
 * Function: CreateINode
 * Description: Allocates and initializes a VFS inode.
 * Parameters:
 *   FileType Type - Inode type.
 *   uint64_t Size - Inode size in bytes.
 *   void* Data - Inode data pointer.
 * Returns:
 *   INode* - Allocated inode pointer.
 */
INode* CreateINode(FileType Type, uint64_t Size, void* Data)
{
    INode* Node               = new INode;
    Node->NodeType            = Type;
    Node->NodeSize            = Size;
    Node->NodeData            = Data;
    Node->IsLazyLoad          = false;
    Node->LazyDataBackedByPMM = false;
    Node->LazyDataPageCount   = 0;
    Node->BackingInodeNumber  = 0;
    Node->LazyLoadRefCount    = 0;
    Node->LazyLoadContext     = nullptr;
    Node->INodeOps            = nullptr;
    Node->FileOps             = &DefaultFileOperations;
    return Node;
}

/**
 * Function: CreateDentry
 * Description: Allocates and initializes a dentry with the provided name, parent, and inode.
 * Parameters:
 *   const char* Name - Dentry name to copy.
 *   Dentry* Parent - Parent dentry pointer.
 *   INode* Node - Inode associated with the dentry.
 * Returns:
 *   Dentry* - Allocated dentry pointer.
 */
Dentry* CreateDentry(const char* Name, Dentry* Parent, INode* Node)
{
    Dentry* NewDentry      = new Dentry;
    NewDentry->name        = DuplicateString(Name);
    NewDentry->inode       = Node;
    NewDentry->parent      = Parent;
    NewDentry->children    = nullptr;
    NewDentry->child_count = 0;
    return NewDentry;
}

/**
 * Function: FindChildBySegment
 * Description: Searches a parent dentry for a child matching a segment name.
 * Parameters:
 *   Dentry* Parent - Parent dentry pointer.
 *   const char* SegmentStart - Segment start pointer.
 *   uint64_t SegmentLength - Segment length in bytes.
 * Returns:
 *   Dentry* - Matching child dentry, or nullptr when not found.
 */
Dentry* FindChildBySegment(Dentry* Parent, const char* SegmentStart, uint64_t SegmentLength)
{
    if (Parent == nullptr)
    {
        return nullptr;
    }

    for (uint64_t ChildIndex = 0; ChildIndex < Parent->child_count; ++ChildIndex)
    {
        Dentry* Child = Parent->children[ChildIndex];
        if (Child != nullptr && MatchSegmentName(Child->name, SegmentStart, SegmentLength))
        {
            return Child;
        }
    }

    return nullptr;
}

bool IsDescendantDentry(const Dentry* CandidateDescendant, const Dentry* CandidateAncestor)
{
    if (CandidateDescendant == nullptr || CandidateAncestor == nullptr)
    {
        return false;
    }

    const Dentry* Current = CandidateDescendant;
    while (Current != nullptr)
    {
        if (Current == CandidateAncestor)
        {
            return true;
        }

        Current = Current->parent;
    }

    return false;
}

void TransferDevDirectoryIfMissing(Dentry* OldRoot, Dentry* NewRoot)
{
    if (OldRoot == nullptr || NewRoot == nullptr || OldRoot == NewRoot)
    {
        return;
    }

    Dentry* DevInOldRoot = FindChildBySegment(OldRoot, DEV_DIRECTORY_NAME, 3);
    if (DevInOldRoot == nullptr || DevInOldRoot->parent == nullptr)
    {
        return;
    }

    if (DevInOldRoot == NewRoot || IsDescendantDentry(NewRoot, DevInOldRoot))
    {
        return;
    }

    Dentry* ExistingDevInNewRoot = FindChildBySegment(NewRoot, DEV_DIRECTORY_NAME, 3);
    if (ExistingDevInNewRoot != nullptr)
    {
        if (ExistingDevInNewRoot->inode == nullptr || ExistingDevInNewRoot->inode->NodeType != INODE_DIR)
        {
            return;
        }

        uint64_t ChildIndex = 0;
        while (ChildIndex < DevInOldRoot->child_count)
        {
            Dentry* Child = DevInOldRoot->children[ChildIndex];
            if (Child == nullptr || Child->name == nullptr)
            {
                ++ChildIndex;
                continue;
            }

            uint64_t ChildNameLength = GetStringLength(Child->name);
            if (ChildNameLength == 0)
            {
                ++ChildIndex;
                continue;
            }

            Dentry* ExistingChild = FindChildBySegment(ExistingDevInNewRoot, Child->name, ChildNameLength);
            if (ExistingChild != nullptr)
            {
                ++ChildIndex;
                continue;
            }

            if (!RemoveChild(DevInOldRoot, Child))
            {
                ++ChildIndex;
                continue;
            }

            if (!AppendChild(ExistingDevInNewRoot, Child))
            {
                AppendChild(DevInOldRoot, Child);
                ++ChildIndex;
            }
        }

        return;
    }

    Dentry* PreviousParent = DevInOldRoot->parent;
    if (!RemoveChild(PreviousParent, DevInOldRoot))
    {
        return;
    }

    if (!AppendChild(NewRoot, DevInOldRoot))
    {
        AppendChild(PreviousParent, DevInOldRoot);
    }
}

/**
 * Function: AppendChild
 * Description: Appends a child dentry to a parent's children array.
 * Parameters:
 *   Dentry* Parent - Parent dentry pointer.
 *   Dentry* Child - Child dentry pointer.
 * Returns:
 *   bool - True when append succeeds.
 */
bool AppendChild(Dentry* Parent, Dentry* Child)
{
    if (Parent == nullptr || Child == nullptr)
    {
        return false;
    }

    uint64_t NewCount       = Parent->child_count + CHILDREN_GROWTH_ONE;
    Dentry** NewChildBuffer = new Dentry*[NewCount];

    for (uint64_t ChildIndex = 0; ChildIndex < Parent->child_count; ++ChildIndex)
    {
        NewChildBuffer[ChildIndex] = Parent->children[ChildIndex];
    }

    NewChildBuffer[Parent->child_count] = Child;

    delete[] Parent->children;
    Parent->children    = NewChildBuffer;
    Parent->child_count = NewCount;
    Child->parent       = Parent;

    return true;
}

bool RemoveChild(Dentry* Parent, Dentry* Child)
{
    if (Parent == nullptr || Child == nullptr || Parent->child_count == 0 || Parent->children == nullptr)
    {
        return false;
    }

    uint64_t ChildIndex = 0;
    while (ChildIndex < Parent->child_count && Parent->children[ChildIndex] != Child)
    {
        ++ChildIndex;
    }

    if (ChildIndex >= Parent->child_count)
    {
        return false;
    }

    uint64_t NewCount = Parent->child_count - 1;
    if (NewCount == 0)
    {
        delete[] Parent->children;
        Parent->children    = nullptr;
        Parent->child_count = 0;
        return true;
    }

    Dentry** NewChildren = new Dentry*[NewCount];
    if (NewChildren == nullptr)
    {
        return false;
    }

    uint64_t NewIndex = 0;
    for (uint64_t Index = 0; Index < Parent->child_count; ++Index)
    {
        if (Index == ChildIndex)
        {
            continue;
        }

        NewChildren[NewIndex] = Parent->children[Index];
        ++NewIndex;
    }

    delete[] Parent->children;
    Parent->children    = NewChildren;
    Parent->child_count = NewCount;
    return true;
}

/**
 * Function: FreeDentryTree
 * Description: Recursively frees a dentry subtree and all associated resources.
 * Parameters:
 *   Dentry* Node - Root of subtree to free.
 * Returns:
 *   void - Does not return a value.
 */
void FreeDentryTree(Dentry* Node)
{
    if (Node == nullptr)
    {
        return;
    }

    for (uint64_t ChildIndex = 0; ChildIndex < Node->child_count; ++ChildIndex)
    {
        FreeDentryTree(Node->children[ChildIndex]);
    }

    delete[] Node->children;
    delete Node->inode;
    delete[] const_cast<char*>(Node->name);
    delete Node;
}

/**
 * Function: IsRootAliasPath
 * Description: Checks whether a path directly refers to the root dentry.
 * Parameters:
 *   const char* NormalizedPath - Normalized path string.
 * Returns:
 *   bool - True when path maps to root.
 */
bool IsRootAliasPath(const char* NormalizedPath)
{
    if (NormalizedPath == nullptr)
    {
        return false;
    }

    if (NormalizedPath[0] == STRING_TERMINATOR)
    {
        return true;
    }

    return NormalizedPath[0] == PATH_DOT && NormalizedPath[1] == STRING_TERMINATOR;
}

/**
 * Function: EnsurePathDentry
 * Description: Ensures that a dentry path exists, creating intermediate directories and optional final node metadata.
 * Parameters:
 *   Dentry* RootDentry - Root dentry pointer.
 *   const char* Path - Entry path.
 *   FileType FinalNodeType - Final node type for the terminal segment.
 *   uint64_t FinalNodeSize - Final node size for the terminal segment.
 *   void* FinalNodeData - Final node data pointer for the terminal segment.
 * Returns:
 *   bool - True when path is represented in the dentry tree.
 */
bool EnsurePathDentry(Dentry* RootDentry, const char* Path, FileType FinalNodeType, uint64_t FinalNodeSize, void* FinalNodeData)
{
    if (RootDentry == nullptr || Path == nullptr)
    {
        return false;
    }

    const char* NormalizedPath = NormalizePathStart(Path);
    if (IsRootAliasPath(NormalizedPath))
    {
        RootDentry->inode->NodeType            = INODE_DIR;
        RootDentry->inode->NodeSize            = 0;
        RootDentry->inode->NodeData            = nullptr;
        RootDentry->inode->IsLazyLoad          = false;
        RootDentry->inode->LazyDataBackedByPMM = false;
        RootDentry->inode->LazyDataPageCount   = 0;
        RootDentry->inode->BackingInodeNumber  = 0;
        RootDentry->inode->LazyLoadRefCount    = 0;
        RootDentry->inode->LazyLoadContext     = nullptr;
        return true;
    }

    Dentry*     Current      = RootDentry;
    const char* SegmentStart = NormalizedPath;

    while (*SegmentStart != STRING_TERMINATOR)
    {
        while (*SegmentStart == PATH_SEPARATOR)
        {
            ++SegmentStart;
        }

        if (*SegmentStart == STRING_TERMINATOR)
        {
            break;
        }

        const char* SegmentEnd = SegmentStart;
        while (*SegmentEnd != STRING_TERMINATOR && *SegmentEnd != PATH_SEPARATOR)
        {
            ++SegmentEnd;
        }

        uint64_t SegmentLength  = static_cast<uint64_t>(SegmentEnd - SegmentStart);
        bool     IsFinalSegment = (*SegmentEnd == STRING_TERMINATOR);

        Dentry* Child = FindChildBySegment(Current, SegmentStart, SegmentLength);
        if (Child == nullptr)
        {
            char*  SegmentName = DuplicateSegment(SegmentStart, SegmentLength);
            INode* ChildNode   = nullptr;

            if (IsFinalSegment)
            {
                ChildNode = CreateINode(FinalNodeType, FinalNodeSize, FinalNodeData);
            }
            else
            {
                ChildNode = CreateINode(INODE_DIR, 0, nullptr);
            }

            Child = CreateDentry(SegmentName, Current, ChildNode);
            delete[] SegmentName;

            if (!AppendChild(Current, Child))
            {
                FreeDentryTree(Child);
                return false;
            }
        }
        else if (IsFinalSegment)
        {
            Child->inode->NodeType            = FinalNodeType;
            Child->inode->NodeSize            = FinalNodeSize;
            Child->inode->NodeData            = FinalNodeData;
            Child->inode->IsLazyLoad          = false;
            Child->inode->LazyDataBackedByPMM = false;
            Child->inode->LazyDataPageCount   = 0;
            Child->inode->BackingInodeNumber  = 0;
            Child->inode->LazyLoadRefCount    = 0;
            Child->inode->LazyLoadContext     = nullptr;
        }

        Current      = Child;
        SegmentStart = SegmentEnd;
    }

    return true;
}

/**
 * Function: MountEntryCallback
 * Description: Adds one initramfs entry into the VFS dentry tree during mount.
 * Parameters:
 *   const RamFileSystemEntry& Entry - Ram filesystem entry.
 *   void* Context - Pointer to MountInitRamFileSystemContext.
 * Returns:
 *   bool - True to continue enumeration, false to stop.
 */
bool MountEntryCallback(const RamFileSystemEntry& Entry, void* Context)
{
    MountInitRamFileSystemContext* MountContext = reinterpret_cast<MountInitRamFileSystemContext*>(Context);
    if (MountContext == nullptr || MountContext->RootDentry == nullptr)
    {
        return false;
    }

    FileType NodeType          = DecodeNodeType(Entry.Type);
    bool     Added             = EnsurePathDentry(MountContext->RootDentry, Entry.Name, NodeType, Entry.Size, const_cast<void*>(Entry.Data));
    MountContext->IsSuccessful = MountContext->IsSuccessful && Added;
    return MountContext->IsSuccessful;
}

bool MountEXTEntryCallback(const ExtendedFileSystemEntry& Entry, void* Context)
{
    MountEXTFileSystemContext* MountContext = reinterpret_cast<MountEXTFileSystemContext*>(Context);
    if (MountContext == nullptr || MountContext->RootDentry == nullptr || MountContext->MountPath == nullptr)
    {
        return false;
    }

    char* TargetPath = ComposeMountPath(MountContext->MountPath, Entry.Name);
    if (TargetPath == nullptr)
    {
        return false;
    }

    FileType NodeType = DecodeNodeType(Entry.Type);
    void*    NodeData = const_cast<void*>(Entry.Data);

    if (NodeType == INODE_DIR)
    {
        NodeData = const_cast<void*>(MountContext->FileSystemManager);
    }

    bool IsMountPointRoot = (Entry.Name != nullptr && Entry.Name[0] == PATH_SEPARATOR && Entry.Name[1] == STRING_TERMINATOR);
    if (IsMountPointRoot)
    {
        NodeData = const_cast<void*>(MountContext->FileSystemManager);
    }

    bool Added = EnsurePathDentry(MountContext->RootDentry, TargetPath, NodeType, Entry.Size, NodeData);

    if (Added)
    {
        Dentry* MountedEntry = ResolvePathInternal(MountContext->RootDentry, MountContext->RootDentry, TargetPath, MAX_SYMLINK_FOLLOW_DEPTH, false);
        if (MountedEntry != nullptr && MountedEntry->inode != nullptr)
        {
            bool IsLazyInode = ((NodeType == INODE_FILE || NodeType == INODE_SYMLINK) && Entry.Data == nullptr && Entry.InodeNumber != 0);
            if (IsLazyInode)
            {
                MountedEntry->inode->IsLazyLoad          = true;
                MountedEntry->inode->LazyDataBackedByPMM = false;
                MountedEntry->inode->LazyDataPageCount   = 0;
                MountedEntry->inode->BackingInodeNumber  = Entry.InodeNumber;
                MountedEntry->inode->LazyLoadRefCount    = 0;
                MountedEntry->inode->LazyLoadContext     = const_cast<void*>(MountContext->FileSystemManager);
            }
            else
            {
                MountedEntry->inode->IsLazyLoad          = false;
                MountedEntry->inode->LazyDataBackedByPMM = false;
                MountedEntry->inode->LazyDataPageCount   = 0;
                MountedEntry->inode->BackingInodeNumber  = 0;
                MountedEntry->inode->LazyLoadRefCount    = 0;
                MountedEntry->inode->LazyLoadContext     = nullptr;
            }
        }
    }

    delete[] TargetPath;

    MountContext->IsSuccessful = MountContext->IsSuccessful && Added;
    return MountContext->IsSuccessful;
}

/**
 * Function: FileTypeToString
 * Description: Converts a VFS file type to a human-readable string.
 * Parameters:
 *   FileType Type - VFS file type value.
 * Returns:
 *   const char* - Type string.
 */
const char* FileTypeToString(FileType Type)
{
    if (Type == INODE_DIR)
    {
        return "dir";
    }

    if (Type == INODE_FILE)
    {
        return "file";
    }

    if (Type == INODE_SYMLINK)
    {
        return "symlink";
    }

    return "dev";
}

/**
 * Function: PrintDentryTree
 * Description: Recursively prints a dentry subtree to the provided console.
 * Parameters:
 *   const Dentry* Entry - Current dentry to print.
 *   TTY* Terminal - Target terminal.
 *   uint64_t Depth - Current tree depth.
 * Returns:
 *   void - Does not return a value.
 */
void PrintDentryTree(const Dentry* Entry, TTY* Terminal, uint64_t Depth)
{
    if (Entry == nullptr || Terminal == nullptr)
    {
        return;
    }

    uint64_t IndentSpaces = Depth * INDENT_SPACES_PER_LEVEL;
    for (uint64_t SpaceIndex = 0; SpaceIndex < IndentSpaces; ++SpaceIndex)
    {
        Terminal->printf_(" ");
    }

    const char* EntryName = Entry->name;
    if (EntryName == nullptr)
    {
        EntryName = "(null)";
    }

    const char* TypeText = "none";
    uint64_t    NodeSize = 0;
    if (Entry->inode != nullptr)
    {
        TypeText = FileTypeToString(Entry->inode->NodeType);
        NodeSize = Entry->inode->NodeSize;
    }

    Terminal->printf_("%s [%s] size=%llu\n", EntryName, TypeText, static_cast<unsigned long long>(NodeSize));

    for (uint64_t ChildIndex = 0; ChildIndex < Entry->child_count; ++ChildIndex)
    {
        PrintDentryTree(Entry->children[ChildIndex], Terminal, Depth + 1);
    }
}
} // namespace

/**
 * Function: VirtualFileSystem
 * Description: Constructs the virtual file system instance.
 * Parameters:
 *   None.
 * Returns:
 *   void - Does not return a value.
 */
VirtualFileSystem::VirtualFileSystem() : Root(nullptr), isEXT(false), ActiveExtendedFileSystem(nullptr)
{
}

/**
 * Function: ~VirtualFileSystem
 * Description: Destroys the virtual file system instance.
 * Parameters:
 *   None.
 * Returns:
 *   void - Does not return a value.
 */
VirtualFileSystem::~VirtualFileSystem()
{
    FreeDentryTree(Root);
    Root = nullptr;
}

/**
 * Function: MountInitRamFileSystem
 * Description: Mounts the initramfs into the virtual file system.
 * Parameters:
 *   RamFileSystemManager* ramFileSystemManager - Manager that provides initramfs entries.
 * Returns:
 *   void - Does not return a value.
 */
void VirtualFileSystem::MountInitRamFileSystem(RamFileSystemManager* ramFileSystemManager)
{
    if (Root != nullptr)
    {
        FreeDentryTree(Root);
        Root = nullptr;
    }

    INode* RootNode          = CreateINode(INODE_DIR, 0, nullptr);
    Root                     = CreateDentry("/", nullptr, RootNode);
    isEXT                    = false;
    ActiveExtendedFileSystem = nullptr;

    if (ramFileSystemManager == nullptr)
    {
        return;
    }

    MountInitRamFileSystemContext MountContext = {};
    MountContext.RootDentry                    = Root;
    MountContext.IsSuccessful                  = true;

    bool EnumerationSuccess = ramFileSystemManager->EnumerateEntries(&MountEntryCallback, &MountContext, nullptr);
    if (!EnumerationSuccess || !MountContext.IsSuccessful)
    {
        return;
    }
}

bool VirtualFileSystem::MountEXTFileSystem(ExtendedFileSystemManager* extendedFileSystemManager, const char* mountPath)
{
    if (extendedFileSystemManager == nullptr || mountPath == nullptr || !extendedFileSystemManager->IsInitialized())
    {
        return false;
    }

    if (Root == nullptr)
    {
        INode* RootNode = CreateINode(INODE_DIR, 0, nullptr);
        Root            = CreateDentry("/", nullptr, RootNode);
    }

    if (!EnsurePathDentry(Root, mountPath, INODE_DIR, 0, extendedFileSystemManager))
    {
        return false;
    }

    MountEXTFileSystemContext MountContext = {};
    MountContext.RootDentry                = Root;
    MountContext.MountPath                 = mountPath;
    MountContext.FileSystemManager         = extendedFileSystemManager;
    MountContext.IsSuccessful              = true;

    bool EnumerationSuccess = extendedFileSystemManager->EnumerateEntries(&MountEXTEntryCallback, &MountContext, nullptr);
    if (!EnumerationSuccess || !MountContext.IsSuccessful)
    {
        return false;
    }

    isEXT                    = true;
    ActiveExtendedFileSystem = extendedFileSystemManager;

    return true;
}

bool VirtualFileSystem::CreateFile(const char* path, FileType type)
{
    if (Root == nullptr || path == nullptr)
    {
        return false;
    }

    Dentry* Existing = Lookup(path);
    if (Existing != nullptr)
    {
        return false;
    }

    bool IsDirectory = (type == INODE_DIR);
    bool IsFile      = (type == INODE_FILE);
    if (!IsDirectory && !IsFile)
    {
        return false;
    }

    if (isEXT && ActiveExtendedFileSystem != nullptr)
    {
        ExtendedFileSystemEntryType EntryType = IsDirectory ? ExtendedFileSystemEntryTypeDirectory : ExtendedFileSystemEntryTypeRegularFile;
        if (!ActiveExtendedFileSystem->CreateFile(path, EntryType))
        {
            return false;
        }

        return EnsurePathDentry(Root, path, type, 0, IsDirectory ? static_cast<void*>(ActiveExtendedFileSystem) : nullptr);
    }

    return EnsurePathDentry(Root, path, type, 0, nullptr);
}

bool VirtualFileSystem::DeleteFile(const char* path, FileType type)
{
    bool IsDirectory = (type == INODE_DIR);
    bool IsFile      = (type == INODE_FILE);
    if (Root == nullptr || path == nullptr || (!IsDirectory && !IsFile))
    {
        return false;
    }

    Dentry* Existing = LookupNoFollowFinal(path);
    if (Existing == nullptr || Existing == Root || Existing->inode == nullptr || Existing->parent == nullptr)
    {
        return false;
    }

    if (Existing->inode->NodeType != type)
    {
        return false;
    }

    if (IsDirectory && Existing->child_count != 0)
    {
        return false;
    }

    if (isEXT && ActiveExtendedFileSystem != nullptr)
    {
        ExtendedFileSystemEntryType EntryType = IsDirectory ? ExtendedFileSystemEntryTypeDirectory : ExtendedFileSystemEntryTypeRegularFile;
        if (!ActiveExtendedFileSystem->DeleteFile(path, EntryType))
        {
            return false;
        }
    }

    if (!RemoveChild(Existing->parent, Existing))
    {
        return false;
    }

    Existing->parent = nullptr;
    FreeDentryTree(Existing);
    return true;
}

bool VirtualFileSystem::RenameFile(const char* oldPath, const char* newPath, FileType type)
{
    bool IsDirectory = (type == INODE_DIR);
    bool IsFile      = (type == INODE_FILE);
    if (Root == nullptr || oldPath == nullptr || newPath == nullptr || (!IsDirectory && !IsFile))
    {
        return false;
    }

    Dentry* Source = LookupNoFollowFinal(oldPath);
    if (Source == nullptr || Source == Root || Source->inode == nullptr || Source->parent == nullptr)
    {
        return false;
    }

    if (Source->inode->NodeType != type)
    {
        return false;
    }

    Dentry* ExistingDestination = LookupNoFollowFinal(newPath);
    if (ExistingDestination != nullptr)
    {
        return ExistingDestination == Source;
    }

    const char* NormalizedNewPath = NormalizePathStart(newPath);
    if (IsRootAliasPath(NormalizedNewPath))
    {
        return false;
    }

    const char* PathEnd = NormalizedNewPath;
    while (*PathEnd != STRING_TERMINATOR)
    {
        ++PathEnd;
    }

    while (PathEnd > NormalizedNewPath && *(PathEnd - 1) == PATH_SEPARATOR)
    {
        --PathEnd;
    }

    if (PathEnd == NormalizedNewPath)
    {
        return false;
    }

    const char* LastSegmentStart = PathEnd;
    while (LastSegmentStart > NormalizedNewPath && *(LastSegmentStart - 1) != PATH_SEPARATOR)
    {
        --LastSegmentStart;
    }

    uint64_t NewNameLength = static_cast<uint64_t>(PathEnd - LastSegmentStart);
    if (NewNameLength == 0)
    {
        return false;
    }

    if ((NewNameLength == 1 && LastSegmentStart[0] == PATH_DOT) || (NewNameLength == 2 && LastSegmentStart[0] == PATH_DOT && LastSegmentStart[1] == PATH_DOT))
    {
        return false;
    }

    Dentry*     TargetParent = Root;
    const char* SegmentStart = NormalizedNewPath;
    while (SegmentStart < LastSegmentStart)
    {
        while (SegmentStart < LastSegmentStart && *SegmentStart == PATH_SEPARATOR)
        {
            ++SegmentStart;
        }

        if (SegmentStart >= LastSegmentStart)
        {
            break;
        }

        const char* SegmentEnd = SegmentStart;
        while (SegmentEnd < LastSegmentStart && *SegmentEnd != PATH_SEPARATOR)
        {
            ++SegmentEnd;
        }

        uint64_t SegmentLength = static_cast<uint64_t>(SegmentEnd - SegmentStart);
        if ((SegmentLength == 1 && SegmentStart[0] == PATH_DOT) || (SegmentLength == 2 && SegmentStart[0] == PATH_DOT && SegmentStart[1] == PATH_DOT))
        {
            return false;
        }

        Dentry* Next = FindChildBySegment(TargetParent, SegmentStart, SegmentLength);
        if (Next == nullptr || Next->inode == nullptr || Next->inode->NodeType != INODE_DIR)
        {
            return false;
        }

        TargetParent = Next;
        SegmentStart = SegmentEnd;
    }

    if (FindChildBySegment(TargetParent, LastSegmentStart, NewNameLength) != nullptr)
    {
        return false;
    }

    if (IsDirectory && (Source == TargetParent || IsDescendantDentry(TargetParent, Source)))
    {
        return false;
    }

    char* NewName = DuplicateSegment(LastSegmentStart, NewNameLength);
    if (NewName == nullptr)
    {
        return false;
    }

    if (isEXT && ActiveExtendedFileSystem != nullptr)
    {
        ExtendedFileSystemEntryType EntryType = IsDirectory ? ExtendedFileSystemEntryTypeDirectory : ExtendedFileSystemEntryTypeRegularFile;
        if (!ActiveExtendedFileSystem->RenameFile(oldPath, newPath, EntryType))
        {
            delete[] NewName;
            return false;
        }
    }

    Dentry* PreviousParent = Source->parent;
    if (!RemoveChild(PreviousParent, Source))
    {
        delete[] NewName;
        return false;
    }

    if (!AppendChild(TargetParent, Source))
    {
        AppendChild(PreviousParent, Source);
        delete[] NewName;
        return false;
    }

    delete[] const_cast<char*>(Source->name);
    Source->name = NewName;
    return true;
}

bool VirtualFileSystem::RegisterDevice(const char* path, void* deviceData, FileOperations* fileOperations)
{
    if (Root == nullptr || path == nullptr || fileOperations == nullptr)
    {
        return false;
    }

    if (!EnsurePathDentry(Root, path, INODE_DEV, 0, deviceData))
    {
        return false;
    }

    Dentry* DeviceDentry = Lookup(path);
    if (DeviceDentry == nullptr || DeviceDentry->inode == nullptr)
    {
        return false;
    }

    DeviceDentry->inode->NodeType = INODE_DEV;
    DeviceDentry->inode->NodeSize = 0;
    DeviceDentry->inode->NodeData = deviceData;
    DeviceDentry->inode->FileOps  = fileOperations;

    return true;
}

bool VirtualFileSystem::SetRoot(Dentry* RootDentry)
{
    if (RootDentry == nullptr || RootDentry->inode == nullptr)
    {
        return false;
    }

    if (RootDentry->inode->NodeType != INODE_DIR)
    {
        return false;
    }

    if (RootDentry->name == nullptr || !(RootDentry->name[0] == PATH_SEPARATOR && RootDentry->name[1] == STRING_TERMINATOR))
    {
        char* RootName = DuplicateString("/");
        if (RootName == nullptr)
        {
            return false;
        }

        if (RootDentry->name != nullptr)
        {
            delete[] const_cast<char*>(RootDentry->name);
        }

        RootDentry->name = RootName;
    }

    Dentry* PreviousRoot = Root;
    TransferDevDirectoryIfMissing(PreviousRoot, RootDentry);

    RootDentry->parent = nullptr;
    Root               = RootDentry;

    if (RootDentry->inode->NodeData != nullptr && RootDentry->inode->NodeData == ActiveExtendedFileSystem)
    {
        isEXT = true;
    }
    else
    {
        isEXT = false;
    }

    return true;
}

/**
 * Function: Lookup
 * Description: Resolves a path to its corresponding dentry in the mounted VFS tree.
 * Parameters:
 *   const char* path - Path to resolve.
 * Returns:
 *   Dentry* - Matching dentry pointer, or nullptr when not found.
 */
Dentry* VirtualFileSystem::Lookup(const char* path)
{
    if (Root == nullptr || path == nullptr)
    {
        return nullptr;
    }

    return ResolvePathInternal(Root, Root, path, MAX_SYMLINK_FOLLOW_DEPTH, true);
}

/**
 * Function: LookupNoFollowFinal
 * Description: Resolves a path while not following a symbolic link in the final path component.
 * Parameters:
 *   const char* path - Path to resolve.
 * Returns:
 *   Dentry* - Matching dentry pointer, or nullptr when not found.
 */
Dentry* VirtualFileSystem::LookupNoFollowFinal(const char* path)
{
    if (Root == nullptr || path == nullptr)
    {
        return nullptr;
    }

    return ResolvePathInternal(Root, Root, path, MAX_SYMLINK_FOLLOW_DEPTH, false);
}

/**
 * Function: PrintVFS
 * Description: Prints the currently mounted VFS tree to the provided console.
 * Parameters:
 *   TTY* Terminal - Target terminal for VFS output.
 * Returns:
 *   void - Does not return a value.
 */
void VirtualFileSystem::PrintVFS(TTY* Terminal)
{
    if (Terminal == nullptr)
    {
        return;
    }

    if (Root == nullptr)
    {
        Terminal->printf_("VFS is empty\n");
        return;
    }

    Terminal->printf_("VFS tree:\n");
    PrintDentryTree(Root, Terminal, ROOT_TREE_DEPTH);
}
