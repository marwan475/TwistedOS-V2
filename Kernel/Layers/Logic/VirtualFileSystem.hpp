/**
 * File: VirtualFileSystem.hpp
 * Author: Marwan Mostafa
 * Description: Virtual file system abstraction layer declarations.
 */

#pragma once

#include <stdint.h>

class RamFileSystemManager;
class FrameBufferConsole;

enum FileType
{
    INODE_FILE,
    INODE_DIR,
    INODE_DEV
};

enum FileFlags
{
    READ,
    WRITE,
    READ_WRITE
};

struct FileOperations
{
};

struct INodeOperations
{
};

struct INode
{
    FileType NodeType;
    uint64_t NodeSize;
    void*    NodeData;
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

    INode* inode;
    struct Dentry* parent;

    struct Dentry** children;
    uint64_t child_count;
};

class VirtualFileSystem
{
private:
    Dentry* Root;

public:
    VirtualFileSystem();
    ~VirtualFileSystem();
    void MountInitRamFileSystem(RamFileSystemManager* ramFileSystemManager);
    void PrintVFS(FrameBufferConsole* Console);
};
