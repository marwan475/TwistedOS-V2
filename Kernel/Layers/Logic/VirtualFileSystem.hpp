/**
 * File: VirtualFileSystem.hpp
 * Author: Marwan Mostafa
 * Description: Virtual file system abstraction layer declarations.
 */

#pragma once

#include <stdint.h>

class RamFileSystemManager;
class ExtendedFileSystemManager;
class TTY;
class VirtualAddressSpace;
class LogicLayer;

enum FileType
{
    INODE_FILE,
    INODE_DIR,
    INODE_SYMLINK,
    INODE_DEV
};

enum FileFlags
{
    READ,
    WRITE,
    READ_WRITE
};

struct File;
struct Dentry;

struct FileOperations
{
    int64_t (*Read)(File* OpenFile, void* Buffer, uint64_t Count);
    int64_t (*Write)(File* OpenFile, const void* Buffer, uint64_t Count);
    int64_t (*Seek)(File* OpenFile, int64_t Offset, int32_t Whence);
    int64_t (*MemoryMap)(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address);
    int64_t (*Ioctl)(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic);
};

struct INodeOperations
{
};

struct INode
{
    FileType         NodeType;
    uint64_t         NodeSize;
    void*            NodeData;
    INodeOperations* INodeOps;
    FileOperations*  FileOps;
};

struct File
{
    uint64_t  FileDescriptor;
    INode*    Node;
    uint64_t  CurrentOffset;
    FileFlags AccessFlags;
    uint64_t  OpenFlags;
    uint64_t  DescriptorFlags;
    Dentry*   DirectoryEntry;
};

struct Dentry
{
    const char* name;

    INode*         inode;
    struct Dentry* parent;

    struct Dentry** children;
    uint64_t        child_count;
};

class VirtualFileSystem
{
private:
    Dentry*                    Root;
    bool                       isEXT;
    ExtendedFileSystemManager* ActiveExtendedFileSystem;

public:
    VirtualFileSystem();
    ~VirtualFileSystem();
    void    MountInitRamFileSystem(RamFileSystemManager* ramFileSystemManager);
    bool    MountEXTFileSystem(ExtendedFileSystemManager* extendedFileSystemManager, const char* mountPath);
    bool    RegisterDevice(const char* path, void* deviceData, FileOperations* fileOperations);
    bool    CreateFile(const char* path, FileType type);
    bool    DeleteFile(const char* path, FileType type);
    bool    RenameFile(const char* oldPath, const char* newPath, FileType type);
    bool    SetRoot(Dentry* RootDentry);
    Dentry* Lookup(const char* path);
    Dentry* LookupNoFollowFinal(const char* path);
    void    PrintVFS(TTY* Terminal);
};
