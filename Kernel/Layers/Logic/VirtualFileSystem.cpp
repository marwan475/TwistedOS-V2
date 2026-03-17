/**
 * File: VirtualFileSystem.cpp
 * Author: Marwan Mostafa
 * Description: Virtual file system abstraction layer implementation.
 */

#include "VirtualFileSystem.hpp"

#include "../Resource/RamFileSystemManager.hpp"
#include <Logging/FrameBufferConsole.hpp>

#include <CommonUtils.hpp>

namespace
{
constexpr char PATH_SEPARATOR          = '/';
constexpr char PATH_DOT                = '.';
constexpr char STRING_TERMINATOR       = '\0';
constexpr uint64_t CHILDREN_GROWTH_ONE = 1;
constexpr uint64_t ROOT_FILE_SIZE       = 0;
constexpr uint64_t ROOT_TREE_DEPTH      = 0;
constexpr uint64_t INDENT_SPACES_PER_LEVEL = 2;

typedef struct
{
	Dentry* RootDentry;
	bool    IsSuccessful;
} MountInitRamFileSystemContext;

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

	return INODE_DEV;
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
	INode* Node    = new INode;
	Node->NodeType = Type;
	Node->NodeSize = Size;
	Node->NodeData = Data;
	Node->INodeOps = nullptr;
	Node->FileOps  = nullptr;
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
	Dentry* NewDentry    = new Dentry;
	NewDentry->name      = DuplicateString(Name);
	NewDentry->inode     = Node;
	NewDentry->parent    = Parent;
	NewDentry->children  = nullptr;
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
	Parent->children   = NewChildBuffer;
	Parent->child_count = NewCount;
	Child->parent      = Parent;

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
		RootDentry->inode->NodeType = INODE_DIR;
		RootDentry->inode->NodeSize = 0;
		RootDentry->inode->NodeData = nullptr;
		return true;
	}

	Dentry* Current = RootDentry;
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

		uint64_t SegmentLength = static_cast<uint64_t>(SegmentEnd - SegmentStart);
		bool     IsFinalSegment = (*SegmentEnd == STRING_TERMINATOR);

		Dentry* Child = FindChildBySegment(Current, SegmentStart, SegmentLength);
		if (Child == nullptr)
		{
			char* SegmentName = DuplicateSegment(SegmentStart, SegmentLength);
			INode* ChildNode  = nullptr;

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
			Child->inode->NodeType = FinalNodeType;
			Child->inode->NodeSize = FinalNodeSize;
			Child->inode->NodeData = FinalNodeData;
		}

		Current = Child;
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

	FileType NodeType = DecodeNodeType(Entry.Type);
	bool     Added    = EnsurePathDentry(MountContext->RootDentry, Entry.Name, NodeType, Entry.Size, const_cast<void*>(Entry.Data));
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

	return "dev";
}

/**
 * Function: PrintDentryTree
 * Description: Recursively prints a dentry subtree to the provided console.
 * Parameters:
 *   const Dentry* Entry - Current dentry to print.
 *   FrameBufferConsole* Console - Target console.
 *   uint64_t Depth - Current tree depth.
 * Returns:
 *   void - Does not return a value.
 */
void PrintDentryTree(const Dentry* Entry, FrameBufferConsole* Console, uint64_t Depth)
{
	if (Entry == nullptr || Console == nullptr)
	{
		return;
	}

	uint64_t IndentSpaces = Depth * INDENT_SPACES_PER_LEVEL;
	for (uint64_t SpaceIndex = 0; SpaceIndex < IndentSpaces; ++SpaceIndex)
	{
		Console->printf_(" ");
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

	Console->printf_("%s [%s] size=%llu\n", EntryName, TypeText, static_cast<unsigned long long>(NodeSize));

	for (uint64_t ChildIndex = 0; ChildIndex < Entry->child_count; ++ChildIndex)
	{
		PrintDentryTree(Entry->children[ChildIndex], Console, Depth + 1);
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
VirtualFileSystem::VirtualFileSystem()
	: Root(nullptr)
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

	INode* RootNode = CreateINode(INODE_DIR, 0, nullptr);
	Root            = CreateDentry("/", nullptr, RootNode);

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

	const char* NormalizedPath = NormalizePathStart(path);
	if (IsRootAliasPath(NormalizedPath))
	{
		return Root;
	}

	Dentry* Current             = Root;
	const char* SegmentStart    = NormalizedPath;

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
		Current                = FindChildBySegment(Current, SegmentStart, SegmentLength);
		if (Current == nullptr)
		{
			return nullptr;
		}

		SegmentStart = SegmentEnd;
	}

	return Current;
}

/**
 * Function: PrintVFS
 * Description: Prints the currently mounted VFS tree to the provided console.
 * Parameters:
 *   FrameBufferConsole* Console - Target console for VFS output.
 * Returns:
 *   void - Does not return a value.
 */
void VirtualFileSystem::PrintVFS(FrameBufferConsole* Console)
{
	if (Console == nullptr)
	{
		return;
	}

	if (Root == nullptr)
	{
		Console->printf_("VFS is empty\n");
		return;
	}

	Console->printf_("VFS tree:\n");
	PrintDentryTree(Root, Console, ROOT_TREE_DEPTH);
}
