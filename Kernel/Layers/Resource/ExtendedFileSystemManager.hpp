/**
 * File: ExtendedFileSystemManager.hpp
 * Author: Marwan Mostafa
 * Description: EXT2 filesystem manager declarations.
 */

#pragma once

#include <stdint.h>

class IDEController;

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

    bool ReadBytesFromDisk(uint32_t OffsetBytes, void* Buffer, uint32_t SizeBytes) const;

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
};