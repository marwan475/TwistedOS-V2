/**
 * File: ExtendedFileSystemManager.cpp
 * Author: Marwan Mostafa
 * Description: EXT2 filesystem manager implementation.
 */

#include "ExtendedFileSystemManager.hpp"

#include "Drivers/IDEController.hpp"

namespace
{
uint16_t ReadLE16(const uint8_t* Data)
{
    return static_cast<uint16_t>(Data[0]) | static_cast<uint16_t>(static_cast<uint16_t>(Data[1]) << 8);
}

uint32_t ReadLE32(const uint8_t* Data)
{
    return static_cast<uint32_t>(Data[0]) | static_cast<uint32_t>(static_cast<uint32_t>(Data[1]) << 8) | static_cast<uint32_t>(static_cast<uint32_t>(Data[2]) << 16) | static_cast<uint32_t>(static_cast<uint32_t>(Data[3]) << 24);
}
} // namespace

ExtendedFileSystemManager::ExtendedFileSystemManager(const IDEController* Controller)
    : Controller(Controller), Initialized(false), PartitionConfigured(false), PartitionStartLBA(0), PartitionSectorCount(0), BlockSizeBytes(0), InodesCount(0), BlocksCount(0)
{
}

bool ExtendedFileSystemManager::ConfigurePartition(uint64_t StartLBA, uint64_t SectorCount)
{
    Initialized          = false;
    PartitionConfigured  = false;
    PartitionStartLBA    = 0;
    PartitionSectorCount = 0;
    BlockSizeBytes       = 0;
    InodesCount          = 0;
    BlocksCount          = 0;

    if (Controller == nullptr || !Controller->IsInitialized() || SectorCount == 0)
    {
        return false;
    }

    PartitionStartLBA    = StartLBA;
    PartitionSectorCount = SectorCount;
    PartitionConfigured  = true;
    return true;
}

bool ExtendedFileSystemManager::ReadBytesFromDisk(uint32_t OffsetBytes, void* Buffer, uint32_t SizeBytes) const
{
    if (Controller == nullptr || Buffer == nullptr || !Controller->IsInitialized() || !PartitionConfigured)
    {
        return false;
    }

    uint32_t BlockSize = Controller->GetBlockSizeBytes();

    if (BlockSize == 0 || (OffsetBytes % BlockSize) != 0 || (SizeBytes % BlockSize) != 0)
    {
        return false;
    }

    uint8_t*  Destination            = static_cast<uint8_t*>(Buffer);
    uint64_t  RelativeStartBlock     = static_cast<uint64_t>(OffsetBytes / BlockSize);
    uint64_t  RelativeBlockCount     = static_cast<uint64_t>(SizeBytes / BlockSize);
    uint64_t  RelativeEndBlock       = RelativeStartBlock + RelativeBlockCount;
    uint64_t  PartitionAvailableBlks = (PartitionSectorCount * Controller->GetBlockSizeBytes()) / BlockSize;

    if (RelativeEndBlock > PartitionAvailableBlks)
    {
        return false;
    }

    for (uint64_t BlockIndex = 0; BlockIndex < RelativeBlockCount; ++BlockIndex)
    {
        uint64_t AbsoluteBlock = PartitionStartLBA + RelativeStartBlock + BlockIndex;

        if (AbsoluteBlock > 0xFFFFFFFFu)
        {
            return false;
        }

        if (!Controller->ReadBlock(static_cast<uint32_t>(AbsoluteBlock), Destination + (BlockIndex * BlockSize)))
        {
            return false;
        }
    }

    return true;
}

bool ExtendedFileSystemManager::Initialize()
{
    Initialized    = false;
    BlockSizeBytes = 0;
    InodesCount    = 0;
    BlocksCount    = 0;

    if (Controller == nullptr || !Controller->IsInitialized() || !PartitionConfigured)
    {
        return false;
    }

    uint8_t SuperBlock[EXT2_SUPERBLOCK_SIZE_BYTES] = {};

    if (!ReadBytesFromDisk(EXT2_SUPERBLOCK_OFFSET_BYTES, SuperBlock, EXT2_SUPERBLOCK_SIZE_BYTES))
    {
        return false;
    }

    const uint16_t Signature = ReadLE16(&SuperBlock[56]);
    if (Signature != EXT2_SIGNATURE)
    {
        return false;
    }

    const uint32_t LogBlockSize = ReadLE32(&SuperBlock[24]);

    InodesCount    = ReadLE32(&SuperBlock[0]);
    BlocksCount    = ReadLE32(&SuperBlock[4]);
    BlockSizeBytes = 1024u << LogBlockSize;
    Initialized    = true;
    return true;
}

bool ExtendedFileSystemManager::IsInitialized() const
{
    return Initialized;
}

uint64_t ExtendedFileSystemManager::GetPartitionStartLBA() const
{
    return PartitionStartLBA;
}

uint64_t ExtendedFileSystemManager::GetPartitionSectorCount() const
{
    return PartitionSectorCount;
}

uint32_t ExtendedFileSystemManager::GetBlockSizeBytes() const
{
    return BlockSizeBytes;
}

uint32_t ExtendedFileSystemManager::GetInodesCount() const
{
    return InodesCount;
}

uint32_t ExtendedFileSystemManager::GetBlocksCount() const
{
    return BlocksCount;
}