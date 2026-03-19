/**
 * File: VirtualFileSystem.hpp
 * Author: Marwan Mostafa
 * Description: Virtual file system abstraction layer declarations.
 */

#pragma once

#include <stdint.h>

class RamFileSystemManager;
class TTY;
class VirtualAddressSpace;

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

struct FileOperations
{
    int64_t (*Read)(File* OpenFile, void* Buffer, uint64_t Count);
    int64_t (*Write)(File* OpenFile, const void* Buffer, uint64_t Count);
    int64_t (*Seek)(File* OpenFile, int64_t Offset, int32_t Whence);
    int64_t (*MemoryMap)(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address);
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
    Dentry* Root;

public:
    VirtualFileSystem();
    ~VirtualFileSystem();
    void    MountInitRamFileSystem(RamFileSystemManager* ramFileSystemManager);
    bool    RegisterDevice(const char* path, void* deviceData, FileOperations* fileOperations);
    Dentry* Lookup(const char* path);
    void    PrintVFS(TTY* Terminal);
};
