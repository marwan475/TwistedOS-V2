/**
 * File: ExtendedFileSystemManager.hpp
 * Author: Marwan Mostafa
 * Description: EXT2 filesystem manager declarations.
 */

#pragma once

#include <stdint.h>

class IDEController;
class TTY;

enum ExtendedFileSystemEntryType
{
    ExtendedFileSystemEntryTypeUnknown = 0,
    ExtendedFileSystemEntryTypeRegularFile,
    ExtendedFileSystemEntryTypeDirectory,
    ExtendedFileSystemEntryTypeSymbolicLink,
    ExtendedFileSystemEntryTypeOther
};

typedef struct
{
    const char*                 Name;
    const void*                 Data;
    uint64_t                    Size;
    ExtendedFileSystemEntryType Type;
    uint32_t                    InodeNumber;
} ExtendedFileSystemEntry;

typedef bool (*ExtendedFileSystemEntryCallback)(const ExtendedFileSystemEntry& Entry, void* Context);

class ExtendedFileSystemManager
{
private:
    static constexpr uint32_t EXT2_SUPERBLOCK_OFFSET_BYTES = 1024;
    static constexpr uint32_t EXT2_SUPERBLOCK_SIZE_BYTES   = 1024;
    static constexpr uint16_t EXT2_SIGNATURE               = 0xEF53;

    const IDEController* Controller;
    bool                 Initialized;
    bool                 PartitionConfigured;
    uint64_t             PartitionStartLBA;
    uint64_t             PartitionSectorCount;
    uint32_t             BlockSizeBytes;
    uint32_t             InodesCount;
    uint32_t             BlocksCount;
    uint32_t             FreeBlocksCount;
    uint32_t             FreeInodesCount;
    uint32_t             FirstDataBlock;
    uint32_t             BlocksPerGroup;
    uint32_t             InodesPerGroup;
    uint32_t             InodeSizeBytes;
    char                 VolumeName[17];

    bool ReadBytesFromDisk(uint32_t OffsetBytes, void* Buffer, uint32_t SizeBytes) const;
    bool WriteBytesToDisk(uint32_t OffsetBytes, const void* Buffer, uint32_t SizeBytes) const;
    bool ReadInode(uint32_t InodeNumber, uint8_t* InodeData, uint32_t InodeDataSize) const;
    bool ReadInodePayload(uint32_t InodeNumber, const uint8_t* InodeData, uint16_t InodeMode, uint64_t PayloadSize, void* DestinationBuffer) const;
    void PrintDirectoryTree(uint32_t DirectoryInodeNumber, TTY* Terminal, uint32_t Depth, uint32_t MaxDepth) const;
    bool EnumerateDirectoryEntries(uint32_t DirectoryInodeNumber, const char* DirectoryPath, ExtendedFileSystemEntryCallback Callback, void* Context) const;

public:
    explicit ExtendedFileSystemManager(const IDEController* Controller);

    bool     ConfigurePartition(uint64_t StartLBA, uint64_t SectorCount);
    bool     Initialize();
    bool     IsInitialized() const;
    uint64_t GetPartitionStartLBA() const;
    uint64_t GetPartitionSectorCount() const;
    uint32_t GetBlockSizeBytes() const;
    uint32_t GetInodesCount() const;
    uint32_t GetBlocksCount() const;
    bool     CreateFile(const char* Path, ExtendedFileSystemEntryType Type);
    bool     DeleteFile(const char* Path, ExtendedFileSystemEntryType Type);
    bool     RenameFile(const char* OldPath, const char* NewPath, ExtendedFileSystemEntryType Type);
    bool     LoadInodeData(uint32_t InodeNumber, void* DestinationBuffer, uint64_t SizeBytes) const;
    bool     LoadInodeDataRange(uint32_t InodeNumber, uint64_t StartOffset, void* DestinationBuffer, uint64_t SizeBytes) const;
    bool     EnumerateEntries(ExtendedFileSystemEntryCallback Callback, void* Context, TTY* Terminal = nullptr) const;
    void     PrintFileSystem(TTY* Terminal) const;
    void     PrintFileTree(TTY* Terminal) const;
};