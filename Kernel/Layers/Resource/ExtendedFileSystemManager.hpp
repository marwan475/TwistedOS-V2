/**
 * File: ExtendedFileSystemManager.hpp
 * Author: Marwan Mostafa
 * Description: EXT2 filesystem manager declarations.
 */

#pragma once

#include <stdint.h>

class IDEController;
class TTY;

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
    bool ReadInode(uint32_t InodeNumber, uint8_t* InodeData, uint32_t InodeDataSize) const;
    void PrintDirectoryTree(uint32_t DirectoryInodeNumber, TTY* Terminal, uint32_t Depth, uint32_t MaxDepth) const;

public:
    explicit ExtendedFileSystemManager(const IDEController* Controller);

    bool ConfigurePartition(uint64_t StartLBA, uint64_t SectorCount);
    bool Initialize();
    bool IsInitialized() const;
    uint64_t GetPartitionStartLBA() const;
    uint64_t GetPartitionSectorCount() const;
    uint32_t GetBlockSizeBytes() const;
    uint32_t GetInodesCount() const;
    uint32_t GetBlocksCount() const;
    void PrintFileSystem(TTY* Terminal) const;
    void PrintFileTree(TTY* Terminal) const;
};