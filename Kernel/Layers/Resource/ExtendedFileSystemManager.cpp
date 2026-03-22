/**
 * File: ExtendedFileSystemManager.cpp
 * Author: Marwan Mostafa
 * Description: EXT2 filesystem manager implementation.
 */

#include "ExtendedFileSystemManager.hpp"

#include "Drivers/IDEController.hpp"
#include "TTY.hpp"

#include <CommonUtils.hpp>

namespace
{
constexpr uint32_t CONTROLLER_SECTOR_SIZE_BYTES       = 512;
constexpr uint32_t EXT2_GROUP_DESCRIPTOR_SIZE         = 32;
constexpr uint32_t EXT2_ROOT_INODE_NUMBER             = 2;
constexpr uint16_t EXT2_INODE_MODE_TYPE_MASK          = 0xF000;
constexpr uint16_t EXT2_INODE_MODE_DIRECTORY          = 0x4000;
constexpr uint16_t EXT2_INODE_MODE_REGULAR_FILE       = 0x8000;
constexpr uint16_t EXT2_INODE_MODE_SYMLINK            = 0xA000;
constexpr uint32_t EXT2_SUPERBLOCK_FREE_BLOCKS_OFFSET = 12;
constexpr uint32_t EXT2_SUPERBLOCK_FREE_INODES_OFFSET = 16;
constexpr uint16_t EXT2_INODE_MODE_DIRECTORY_0755     = 0x41ED;
constexpr uint16_t EXT2_INODE_MODE_REGULAR_0644       = 0x81A4;

ExtendedFileSystemEntryType DecodeEntryType(uint8_t Type)
{
    switch (Type)
    {
        case 1:
            return ExtendedFileSystemEntryTypeRegularFile;
        case 2:
            return ExtendedFileSystemEntryTypeDirectory;
        case 7:
            return ExtendedFileSystemEntryTypeSymbolicLink;
        default:
            return ExtendedFileSystemEntryTypeOther;
    }
}

uint16_t ReadLE16(const uint8_t* Data)
{
    return static_cast<uint16_t>(Data[0]) | static_cast<uint16_t>(static_cast<uint16_t>(Data[1]) << 8);
}

uint32_t ReadLE32(const uint8_t* Data)
{
    return static_cast<uint32_t>(Data[0]) | static_cast<uint32_t>(static_cast<uint32_t>(Data[1]) << 8) | static_cast<uint32_t>(static_cast<uint32_t>(Data[2]) << 16)
           | static_cast<uint32_t>(static_cast<uint32_t>(Data[3]) << 24);
}

void WriteLE16(uint8_t* Data, uint16_t Value)
{
    Data[0] = static_cast<uint8_t>(Value & 0xFFu);
    Data[1] = static_cast<uint8_t>((Value >> 8) & 0xFFu);
}

void WriteLE32(uint8_t* Data, uint32_t Value)
{
    Data[0] = static_cast<uint8_t>(Value & 0xFFu);
    Data[1] = static_cast<uint8_t>((Value >> 8) & 0xFFu);
    Data[2] = static_cast<uint8_t>((Value >> 16) & 0xFFu);
    Data[3] = static_cast<uint8_t>((Value >> 24) & 0xFFu);
}

uint16_t AlignTo4(uint16_t Value)
{
    return static_cast<uint16_t>((Value + 3u) & static_cast<uint16_t>(~3u));
}

const char* Ext2DirectoryEntryTypeToString(uint8_t Type)
{
    switch (Type)
    {
        case 1:
            return "file";
        case 2:
            return "dir";
        case 7:
            return "symlink";
        default:
            return "other";
    }
}

} // namespace

ExtendedFileSystemManager::ExtendedFileSystemManager(const IDEController* Controller)
    : Controller(Controller), Initialized(false), PartitionConfigured(false), PartitionStartLBA(0), PartitionSectorCount(0), BlockSizeBytes(0), InodesCount(0), BlocksCount(0), FreeBlocksCount(0),
      FreeInodesCount(0), FirstDataBlock(0), BlocksPerGroup(0), InodesPerGroup(0), InodeSizeBytes(0), VolumeName{}
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
    FreeBlocksCount      = 0;
    FreeInodesCount      = 0;
    FirstDataBlock       = 0;
    BlocksPerGroup       = 0;
    InodesPerGroup       = 0;
    InodeSizeBytes       = 0;

    for (uint32_t Index = 0; Index < 17; ++Index)
    {
        VolumeName[Index] = '\0';
    }

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

    if (SizeBytes == 0)
    {
        return true;
    }

    uint32_t SectorSize = Controller->GetBlockSizeBytes();
    if (SectorSize == 0)
    {
        return false;
    }

    uint64_t StartSector        = static_cast<uint64_t>(OffsetBytes) / SectorSize;
    uint64_t EndOffsetExclusive = static_cast<uint64_t>(OffsetBytes) + SizeBytes;
    uint64_t EndSectorInclusive = (EndOffsetExclusive - 1) / SectorSize;

    if (StartSector >= PartitionSectorCount || EndSectorInclusive >= PartitionSectorCount)
    {
        return false;
    }

    uint8_t* Destination = static_cast<uint8_t*>(Buffer);
    uint64_t BytesCopied = 0;

    for (uint64_t SectorIndex = StartSector; SectorIndex <= EndSectorInclusive; ++SectorIndex)
    {
        uint64_t AbsoluteSector = PartitionStartLBA + SectorIndex;

        if (AbsoluteSector > 0xFFFFFFFFu)
        {
            return false;
        }

        uint8_t SectorBuffer[CONTROLLER_SECTOR_SIZE_BYTES] = {};
        if (!Controller->ReadBlock(static_cast<uint32_t>(AbsoluteSector), SectorBuffer))
        {
            return false;
        }

        uint32_t SectorStartOffset = (SectorIndex == StartSector) ? (OffsetBytes % SectorSize) : 0;
        uint32_t SectorAvailable   = SectorSize - SectorStartOffset;
        uint64_t Remaining         = static_cast<uint64_t>(SizeBytes) - BytesCopied;
        uint32_t BytesToCopy       = (Remaining < SectorAvailable) ? static_cast<uint32_t>(Remaining) : SectorAvailable;

        memcpy(Destination + BytesCopied, SectorBuffer + SectorStartOffset, BytesToCopy);
        BytesCopied += BytesToCopy;
    }

    return true;
}

bool ExtendedFileSystemManager::WriteBytesToDisk(uint32_t OffsetBytes, const void* Buffer, uint32_t SizeBytes) const
{
    if (Controller == nullptr || Buffer == nullptr || !Controller->IsInitialized() || !PartitionConfigured)
    {
        return false;
    }

    if (SizeBytes == 0)
    {
        return true;
    }

    uint32_t SectorSize = Controller->GetBlockSizeBytes();
    if (SectorSize == 0)
    {
        return false;
    }

    uint64_t StartSector        = static_cast<uint64_t>(OffsetBytes) / SectorSize;
    uint64_t EndOffsetExclusive = static_cast<uint64_t>(OffsetBytes) + SizeBytes;
    uint64_t EndSectorInclusive = (EndOffsetExclusive - 1) / SectorSize;

    if (StartSector >= PartitionSectorCount || EndSectorInclusive >= PartitionSectorCount)
    {
        return false;
    }

    const uint8_t* Source       = static_cast<const uint8_t*>(Buffer);
    uint64_t       BytesWritten = 0;

    for (uint64_t SectorIndex = StartSector; SectorIndex <= EndSectorInclusive; ++SectorIndex)
    {
        uint64_t AbsoluteSector = PartitionStartLBA + SectorIndex;
        if (AbsoluteSector > 0xFFFFFFFFu)
        {
            return false;
        }

        uint8_t SectorBuffer[CONTROLLER_SECTOR_SIZE_BYTES] = {};

        uint32_t SectorStartOffset = (SectorIndex == StartSector) ? (OffsetBytes % SectorSize) : 0;
        uint32_t SectorAvailable   = SectorSize - SectorStartOffset;
        uint64_t Remaining         = static_cast<uint64_t>(SizeBytes) - BytesWritten;
        uint32_t BytesToWrite      = (Remaining < SectorAvailable) ? static_cast<uint32_t>(Remaining) : SectorAvailable;

        if (SectorStartOffset != 0 || BytesToWrite != SectorSize)
        {
            if (!Controller->ReadBlock(static_cast<uint32_t>(AbsoluteSector), SectorBuffer))
            {
                return false;
            }
        }

        memcpy(SectorBuffer + SectorStartOffset, Source + BytesWritten, BytesToWrite);
        if (!Controller->WriteBlock(static_cast<uint32_t>(AbsoluteSector), SectorBuffer))
        {
            return false;
        }

        BytesWritten += BytesToWrite;
    }

    return true;
}

bool ExtendedFileSystemManager::ReadInode(uint32_t InodeNumber, uint8_t* InodeData, uint32_t InodeDataSize) const
{
    if (!Initialized || InodeData == nullptr || InodeDataSize < InodeSizeBytes || InodeNumber == 0 || InodesPerGroup == 0 || BlockSizeBytes == 0)
    {
        return false;
    }

    uint32_t ZeroBasedInode = InodeNumber - 1;
    uint32_t GroupIndex     = ZeroBasedInode / InodesPerGroup;
    uint32_t InodeIndex     = ZeroBasedInode % InodesPerGroup;

    uint32_t GroupDescriptorTableOffset = (BlockSizeBytes == 1024u) ? (2u * BlockSizeBytes) : BlockSizeBytes;
    uint32_t GroupDescriptorOffset      = GroupDescriptorTableOffset + (GroupIndex * EXT2_GROUP_DESCRIPTOR_SIZE);

    uint8_t GroupDescriptor[EXT2_GROUP_DESCRIPTOR_SIZE] = {};
    if (!ReadBytesFromDisk(GroupDescriptorOffset, GroupDescriptor, sizeof(GroupDescriptor)))
    {
        return false;
    }

    uint32_t InodeTableBlock = ReadLE32(&GroupDescriptor[8]);
    if (InodeTableBlock == 0)
    {
        return false;
    }

    uint64_t InodeOffset = (static_cast<uint64_t>(InodeTableBlock) * BlockSizeBytes) + (static_cast<uint64_t>(InodeIndex) * InodeSizeBytes);
    if (InodeOffset > 0xFFFFFFFFu)
    {
        return false;
    }

    bool ReadOk = ReadBytesFromDisk(static_cast<uint32_t>(InodeOffset), InodeData, InodeSizeBytes);
    return ReadOk;
}

bool ExtendedFileSystemManager::ReadInodePayload(uint32_t InodeNumber, const uint8_t* InodeData, uint16_t InodeMode, uint64_t PayloadSize, void* DestinationBuffer) const
{
    static_cast<void>(InodeNumber);

    if (!Initialized || InodeData == nullptr || (DestinationBuffer == nullptr && PayloadSize != 0))
    {
        return false;
    }

    if (PayloadSize == 0)
    {
        return true;
    }

    uint8_t* Destination = reinterpret_cast<uint8_t*>(DestinationBuffer);
    kmemset(Destination, 0, static_cast<size_t>(PayloadSize));

    if ((InodeMode & 0xF000) == EXT2_INODE_MODE_SYMLINK && PayloadSize <= 60)
    {
        memcpy(Destination, &InodeData[40], static_cast<size_t>(PayloadSize));
        return true;
    }

    if (BlockSizeBytes == 0 || (BlockSizeBytes % 4) != 0)
    {
        return false;
    }

    uint32_t PointersPerBlock = BlockSizeBytes / 4;
    uint64_t SingleSpan       = static_cast<uint64_t>(PointersPerBlock);
    uint64_t DoubleSpan       = SingleSpan * SingleSpan;
    uint64_t TripleSpan       = DoubleSpan * SingleSpan;

    auto ResolveDataBlockNumber = [&](uint64_t LogicalBlockIndex) -> uint32_t
    {
        auto ReadPointerFromBlock = [&](uint32_t PointerBlockNumber, uint32_t PointerIndex) -> uint32_t
        {
            if (PointerBlockNumber == 0 || PointerIndex >= PointersPerBlock)
            {
                return 0;
            }

            uint64_t BlockByteOffset = static_cast<uint64_t>(PointerBlockNumber) * BlockSizeBytes;
            if (BlockByteOffset > 0xFFFFFFFFu)
            {
                return 0;
            }

            uint8_t* PointerBlockData = new uint8_t[BlockSizeBytes];
            if (PointerBlockData == nullptr)
            {
                return 0;
            }

            bool ReadSuccess = ReadBytesFromDisk(static_cast<uint32_t>(BlockByteOffset), PointerBlockData, BlockSizeBytes);
            if (!ReadSuccess)
            {
                delete[] PointerBlockData;
                return 0;
            }

            uint32_t Value = ReadLE32(&PointerBlockData[PointerIndex * 4]);
            delete[] PointerBlockData;
            return Value;
        };

        if (LogicalBlockIndex < 12)
        {
            return ReadLE32(&InodeData[40 + (LogicalBlockIndex * 4)]);
        }

        LogicalBlockIndex -= 12;

        if (LogicalBlockIndex < SingleSpan)
        {
            uint32_t SingleIndirectBlock = ReadLE32(&InodeData[88]);
            return ReadPointerFromBlock(SingleIndirectBlock, static_cast<uint32_t>(LogicalBlockIndex));
        }

        LogicalBlockIndex -= SingleSpan;

        if (LogicalBlockIndex < DoubleSpan)
        {
            uint32_t DoubleIndirectBlock = ReadLE32(&InodeData[92]);
            uint32_t FirstLevelIndex     = static_cast<uint32_t>(LogicalBlockIndex / SingleSpan);
            uint32_t SecondLevelIndex    = static_cast<uint32_t>(LogicalBlockIndex % SingleSpan);
            uint32_t FirstLevelBlock     = ReadPointerFromBlock(DoubleIndirectBlock, FirstLevelIndex);
            return ReadPointerFromBlock(FirstLevelBlock, SecondLevelIndex);
        }

        LogicalBlockIndex -= DoubleSpan;

        if (LogicalBlockIndex < TripleSpan)
        {
            uint32_t TripleIndirectBlock = ReadLE32(&InodeData[96]);
            uint32_t FirstLevelIndex     = static_cast<uint32_t>(LogicalBlockIndex / DoubleSpan);
            uint64_t Remainder           = LogicalBlockIndex % DoubleSpan;
            uint32_t SecondLevelIndex    = static_cast<uint32_t>(Remainder / SingleSpan);
            uint32_t ThirdLevelIndex     = static_cast<uint32_t>(Remainder % SingleSpan);
            uint32_t FirstLevelBlock     = ReadPointerFromBlock(TripleIndirectBlock, FirstLevelIndex);
            uint32_t SecondLevelBlock    = ReadPointerFromBlock(FirstLevelBlock, SecondLevelIndex);
            return ReadPointerFromBlock(SecondLevelBlock, ThirdLevelIndex);
        }

        return 0;
    };

    uint64_t RemainingBytes = PayloadSize;
    uint64_t LogicalBlock   = 0;
    uint64_t WriteOffset    = 0;

    while (RemainingBytes > 0)
    {
        uint64_t BytesThisBlock = (RemainingBytes < BlockSizeBytes) ? RemainingBytes : BlockSizeBytes;
        uint32_t DataBlock      = ResolveDataBlockNumber(LogicalBlock);

        if (DataBlock != 0)
        {
            uint64_t BlockByteOffset = static_cast<uint64_t>(DataBlock) * BlockSizeBytes;
            if (BlockByteOffset > 0xFFFFFFFFu)
            {
                return false;
            }

            if (!ReadBytesFromDisk(static_cast<uint32_t>(BlockByteOffset), Destination + WriteOffset, static_cast<uint32_t>(BytesThisBlock)))
            {
                return false;
            }
        }

        RemainingBytes -= BytesThisBlock;
        WriteOffset += BytesThisBlock;
        ++LogicalBlock;
    }
    return true;
}

bool ExtendedFileSystemManager::EnumerateDirectoryEntries(uint32_t DirectoryInodeNumber, const char* DirectoryPath, ExtendedFileSystemEntryCallback Callback, void* Context, TTY* Terminal,
                                                          uint64_t* EnumeratedEntries) const
{
    if (DirectoryPath == nullptr || Callback == nullptr)
    {
        return false;
    }

    uint8_t InodeBuffer[256] = {};
    if (InodeSizeBytes > sizeof(InodeBuffer) || !ReadInode(DirectoryInodeNumber, InodeBuffer, sizeof(InodeBuffer)))
    {
        return false;
    }

    uint16_t Mode = ReadLE16(&InodeBuffer[0]);
    if ((Mode & EXT2_INODE_MODE_DIRECTORY) == 0)
    {
        return false;
    }

    uint32_t DirectBlockOffset = 40;
    for (uint32_t BlockPointerIndex = 0; BlockPointerIndex < 12; ++BlockPointerIndex)
    {
        uint32_t BlockNumber = ReadLE32(&InodeBuffer[DirectBlockOffset + (BlockPointerIndex * 4)]);
        if (BlockNumber == 0)
        {
            continue;
        }

        uint64_t BlockByteOffset = static_cast<uint64_t>(BlockNumber) * BlockSizeBytes;
        if (BlockByteOffset > 0xFFFFFFFFu)
        {
            continue;
        }

        uint8_t* BlockData = new uint8_t[BlockSizeBytes];
        if (BlockData == nullptr)
        {
            return false;
        }

        bool ReadSuccess = ReadBytesFromDisk(static_cast<uint32_t>(BlockByteOffset), BlockData, BlockSizeBytes);
        if (!ReadSuccess)
        {
            delete[] BlockData;
            continue;
        }

        uint32_t Offset = 0;
        while (Offset + 8 <= BlockSizeBytes)
        {
            uint32_t EntryInode  = ReadLE32(&BlockData[Offset]);
            uint16_t EntryLength = ReadLE16(&BlockData[Offset + 4]);
            uint8_t  NameLength  = BlockData[Offset + 6];
            uint8_t  EntryType   = BlockData[Offset + 7];

            if (EntryLength < 8 || (Offset + EntryLength) > BlockSizeBytes)
            {
                break;
            }

            if (EntryInode != 0 && NameLength > 0 && (8u + NameLength) <= EntryLength)
            {
                char Name[256] = {};
                for (uint32_t NameIndex = 0; NameIndex < NameLength && NameIndex < (sizeof(Name) - 1); ++NameIndex)
                {
                    Name[NameIndex] = static_cast<char>(BlockData[Offset + 8 + NameIndex]);
                }
                Name[(NameLength < (sizeof(Name) - 1)) ? NameLength : (sizeof(Name) - 1)] = '\0';

                bool IsDot    = (NameLength == 1 && Name[0] == '.');
                bool IsDotDot = (NameLength == 2 && Name[0] == '.' && Name[1] == '.');
                if (!IsDot && !IsDotDot)
                {
                    uint64_t EntrySize           = 0;
                    bool     IsDirectory         = false;
                    uint16_t ChildMode           = 0;
                    uint8_t  ChildInodeData[256] = {};

                    if (EntryInode <= InodesCount && InodeSizeBytes <= sizeof(ChildInodeData) && ReadInode(EntryInode, ChildInodeData, sizeof(ChildInodeData)))
                    {
                        EntrySize   = ReadLE32(&ChildInodeData[4]);
                        ChildMode   = ReadLE16(&ChildInodeData[0]);
                        IsDirectory = ((ChildMode & EXT2_INODE_MODE_DIRECTORY) != 0);
                    }

                    uint64_t DirectoryPathLength = strlen(DirectoryPath);
                    uint64_t NameLengthSafe      = strlen(Name);
                    bool     NeedsSeparator      = (DirectoryPathLength > 1 && DirectoryPath[DirectoryPathLength - 1] != '/');
                    uint64_t FullPathLength      = DirectoryPathLength + (NeedsSeparator ? 1 : 0) + NameLengthSafe;
                    char*    FullPath            = new char[FullPathLength + 1];

                    uint64_t Cursor = 0;
                    memcpy(FullPath + Cursor, DirectoryPath, static_cast<size_t>(DirectoryPathLength));
                    Cursor += DirectoryPathLength;

                    if (NeedsSeparator)
                    {
                        FullPath[Cursor] = '/';
                        ++Cursor;
                    }

                    memcpy(FullPath + Cursor, Name, static_cast<size_t>(NameLengthSafe));
                    Cursor += NameLengthSafe;
                    FullPath[Cursor] = '\0';

                    ExtendedFileSystemEntryType DecodedType = DecodeEntryType(EntryType);
                    if (DecodedType == ExtendedFileSystemEntryTypeOther)
                    {
                        DecodedType = IsDirectory ? ExtendedFileSystemEntryTypeDirectory : ExtendedFileSystemEntryTypeRegularFile;
                    }

                    ExtendedFileSystemEntry Entry = {};
                    Entry.Name                    = FullPath;
                    void* EntryData               = nullptr;

                    if (EnumeratedEntries != nullptr)
                    {
                        ++(*EnumeratedEntries);
                        if (Terminal != nullptr)
                        {
                            if ((*EnumeratedEntries <= 16) || ((*EnumeratedEntries % 512) == 0))
                            {
                                Terminal->printf_("ext enum dbg: inode=%u type=%u size=%llu path=%s count=%llu\n", EntryInode, static_cast<unsigned int>(DecodedType),
                                                  static_cast<unsigned long long>(EntrySize), FullPath, static_cast<unsigned long long>(*EnumeratedEntries));
                            }
                        }
                    }

                    Entry.Data        = EntryData;
                    Entry.Size        = EntrySize;
                    Entry.Type        = DecodedType;
                    Entry.InodeNumber = EntryInode;

                    bool ContinueEnumeration = Callback(Entry, Context);

                    bool RecurseIntoDirectory = (DecodedType == ExtendedFileSystemEntryTypeDirectory || IsDirectory);
                    if (ContinueEnumeration && RecurseIntoDirectory)
                    {
                        ContinueEnumeration = EnumerateDirectoryEntries(EntryInode, FullPath, Callback, Context, Terminal, EnumeratedEntries);
                    }

                    delete[] FullPath;

                    if (!ContinueEnumeration)
                    {
                        delete[] BlockData;
                        return false;
                    }
                }
            }

            Offset += EntryLength;
        }

        delete[] BlockData;
    }

    return true;
}

void ExtendedFileSystemManager::PrintDirectoryTree(uint32_t DirectoryInodeNumber, TTY* Terminal, uint32_t Depth, uint32_t MaxDepth) const
{
    if (Terminal == nullptr || Depth > MaxDepth)
    {
        return;
    }

    uint8_t InodeBuffer[256] = {};
    if (InodeSizeBytes > sizeof(InodeBuffer) || !ReadInode(DirectoryInodeNumber, InodeBuffer, sizeof(InodeBuffer)))
    {
        return;
    }

    uint16_t Mode = ReadLE16(&InodeBuffer[0]);
    if ((Mode & EXT2_INODE_MODE_DIRECTORY) == 0)
    {
        return;
    }

    uint32_t DirectBlockOffset = 40;
    for (uint32_t BlockPointerIndex = 0; BlockPointerIndex < 12; ++BlockPointerIndex)
    {
        uint32_t BlockNumber = ReadLE32(&InodeBuffer[DirectBlockOffset + (BlockPointerIndex * 4)]);
        if (BlockNumber == 0)
        {
            continue;
        }

        uint64_t BlockByteOffset = static_cast<uint64_t>(BlockNumber) * BlockSizeBytes;
        if (BlockByteOffset > 0xFFFFFFFFu)
        {
            continue;
        }

        uint8_t* BlockData = new uint8_t[BlockSizeBytes];
        if (BlockData == nullptr)
        {
            return;
        }

        bool ReadSuccess = ReadBytesFromDisk(static_cast<uint32_t>(BlockByteOffset), BlockData, BlockSizeBytes);
        if (!ReadSuccess)
        {
            delete[] BlockData;
            continue;
        }

        uint32_t Offset = 0;
        while (Offset + 8 <= BlockSizeBytes)
        {
            uint32_t EntryInode  = ReadLE32(&BlockData[Offset]);
            uint16_t EntryLength = ReadLE16(&BlockData[Offset + 4]);
            uint8_t  NameLength  = BlockData[Offset + 6];
            uint8_t  EntryType   = BlockData[Offset + 7];

            if (EntryLength < 8 || (Offset + EntryLength) > BlockSizeBytes)
            {
                break;
            }

            if (EntryInode != 0 && NameLength > 0 && (8u + NameLength) <= EntryLength)
            {
                char Name[256] = {};
                for (uint32_t NameIndex = 0; NameIndex < NameLength; ++NameIndex)
                {
                    Name[NameIndex] = static_cast<char>(BlockData[Offset + 8 + NameIndex]);
                }
                Name[NameLength] = '\0';

                bool IsDot    = (NameLength == 1 && Name[0] == '.');
                bool IsDotDot = (NameLength == 2 && Name[0] == '.' && Name[1] == '.');
                if (!IsDot && !IsDotDot)
                {
                    for (uint32_t SpaceIndex = 0; SpaceIndex < ((Depth + 1) * 2); ++SpaceIndex)
                    {
                        Terminal->printf_(" ");
                    }

                    uint64_t EntrySize = 0;
                    if (EntryInode <= InodesCount)
                    {
                        uint8_t ChildInodeBuffer[256] = {};
                        if (InodeSizeBytes <= sizeof(ChildInodeBuffer) && ReadInode(EntryInode, ChildInodeBuffer, sizeof(ChildInodeBuffer)))
                        {
                            EntrySize = ReadLE32(&ChildInodeBuffer[4]);
                        }
                    }

                    Terminal->printf_("%s [%s] size=%llu\n", Name, Ext2DirectoryEntryTypeToString(EntryType), static_cast<unsigned long long>(EntrySize));

                    if (EntryType == 2 && Depth < MaxDepth)
                    {
                        PrintDirectoryTree(EntryInode, Terminal, Depth + 1, MaxDepth);
                    }
                }
            }

            Offset += EntryLength;
        }

        delete[] BlockData;
    }
}

bool ExtendedFileSystemManager::Initialize()
{
    Initialized     = false;
    BlockSizeBytes  = 0;
    InodesCount     = 0;
    BlocksCount     = 0;
    FreeBlocksCount = 0;
    FreeInodesCount = 0;
    FirstDataBlock  = 0;
    BlocksPerGroup  = 0;
    InodesPerGroup  = 0;

    for (uint32_t Index = 0; Index < 17; ++Index)
    {
        VolumeName[Index] = '\0';
    }

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

    InodesCount     = ReadLE32(&SuperBlock[0]);
    BlocksCount     = ReadLE32(&SuperBlock[4]);
    FreeBlocksCount = ReadLE32(&SuperBlock[12]);
    FreeInodesCount = ReadLE32(&SuperBlock[16]);
    FirstDataBlock  = ReadLE32(&SuperBlock[20]);
    BlocksPerGroup  = ReadLE32(&SuperBlock[32]);
    InodesPerGroup  = ReadLE32(&SuperBlock[40]);
    InodeSizeBytes  = ReadLE16(&SuperBlock[88]);
    BlockSizeBytes  = 1024u << LogBlockSize;

    if (InodeSizeBytes == 0)
    {
        InodeSizeBytes = 128;
    }

    for (uint32_t Index = 0; Index < 16; ++Index)
    {
        VolumeName[Index] = static_cast<char>(SuperBlock[120 + Index]);
    }
    VolumeName[16] = '\0';

    Initialized = true;
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

bool ExtendedFileSystemManager::LoadInodeData(uint32_t InodeNumber, void* DestinationBuffer, uint64_t SizeBytes) const
{
    if (!Initialized || DestinationBuffer == nullptr || SizeBytes == 0)
    {
        return false;
    }

    uint8_t InodeBuffer[256] = {};
    if (InodeSizeBytes > sizeof(InodeBuffer) || !ReadInode(InodeNumber, InodeBuffer, sizeof(InodeBuffer)))
    {
        return false;
    }

    uint64_t InodeSize = ReadLE32(&InodeBuffer[4]);
    if (SizeBytes > InodeSize)
    {
        return false;
    }

    uint16_t InodeMode = ReadLE16(&InodeBuffer[0]);
    return ReadInodePayload(InodeNumber, InodeBuffer, InodeMode, SizeBytes, DestinationBuffer);
}

bool ExtendedFileSystemManager::CreateFile(const char* Path, ExtendedFileSystemEntryType Type)
{
    bool CreatingDirectory   = (Type == ExtendedFileSystemEntryTypeDirectory);
    bool CreatingRegularFile = (Type == ExtendedFileSystemEntryTypeRegularFile);
    if (!CreatingDirectory && !CreatingRegularFile)
    {
        return false;
    }

    if (!Initialized || Path == nullptr || BlockSizeBytes == 0 || InodeSizeBytes == 0 || InodesPerGroup == 0 || BlocksPerGroup == 0)
    {
        return false;
    }

    const char* EffectivePath = Path;
    while (*EffectivePath == '/')
    {
        ++EffectivePath;
    }

    if (*EffectivePath == '\0')
    {
        return false;
    }

    constexpr uint32_t MAX_PATH_CHARS    = 1024;
    constexpr uint32_t MAX_PATH_SEGMENTS = 64;
    constexpr uint32_t MAX_SEGMENT_CHARS = 255;
    char               PathBuffer[MAX_PATH_CHARS];
    kmemset(PathBuffer, 0, sizeof(PathBuffer));

    uint32_t PathLength = 0;
    while (EffectivePath[PathLength] != '\0' && PathLength < (MAX_PATH_CHARS - 1))
    {
        PathBuffer[PathLength] = EffectivePath[PathLength];
        ++PathLength;
    }
    PathBuffer[PathLength] = '\0';

    while (PathLength > 0 && PathBuffer[PathLength - 1] == '/')
    {
        PathBuffer[PathLength - 1] = '\0';
        --PathLength;
    }

    if (PathLength == 0)
    {
        return false;
    }

    char     Segments[MAX_PATH_SEGMENTS][MAX_SEGMENT_CHARS + 1];
    uint32_t SegmentLengths[MAX_PATH_SEGMENTS];
    kmemset(Segments, 0, sizeof(Segments));
    kmemset(SegmentLengths, 0, sizeof(SegmentLengths));
    uint32_t SegmentCount = 0;

    uint32_t Cursor = 0;
    while (Cursor < PathLength)
    {
        while (Cursor < PathLength && PathBuffer[Cursor] == '/')
        {
            ++Cursor;
        }

        if (Cursor >= PathLength)
        {
            break;
        }

        if (SegmentCount >= MAX_PATH_SEGMENTS)
        {
            return false;
        }

        uint32_t SegmentStart = Cursor;
        while (Cursor < PathLength && PathBuffer[Cursor] != '/')
        {
            ++Cursor;
        }

        uint32_t SegmentLength = Cursor - SegmentStart;
        if (SegmentLength == 0 || SegmentLength > MAX_SEGMENT_CHARS)
        {
            return false;
        }

        for (uint32_t NameIndex = 0; NameIndex < SegmentLength; ++NameIndex)
        {
            Segments[SegmentCount][NameIndex] = PathBuffer[SegmentStart + NameIndex];
        }
        Segments[SegmentCount][SegmentLength] = '\0';
        SegmentLengths[SegmentCount]          = SegmentLength;
        ++SegmentCount;
    }

    if (SegmentCount == 0)
    {
        return false;
    }

    const char* NewEntryName      = Segments[SegmentCount - 1];
    uint32_t    NewEntryNameBytes = SegmentLengths[SegmentCount - 1];

    if ((NewEntryNameBytes == 1 && NewEntryName[0] == '.') || (NewEntryNameBytes == 2 && NewEntryName[0] == '.' && NewEntryName[1] == '.'))
    {
        return false;
    }

    auto ReadGroupDescriptor = [&](uint32_t GroupIndex, uint8_t* GroupDescriptor) -> bool
    {
        uint32_t GroupDescriptorTableOffset = (BlockSizeBytes == 1024u) ? (2u * BlockSizeBytes) : BlockSizeBytes;
        uint64_t GroupDescriptorOffset64    = static_cast<uint64_t>(GroupDescriptorTableOffset) + (static_cast<uint64_t>(GroupIndex) * EXT2_GROUP_DESCRIPTOR_SIZE);
        if (GroupDescriptorOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        return ReadBytesFromDisk(static_cast<uint32_t>(GroupDescriptorOffset64), GroupDescriptor, EXT2_GROUP_DESCRIPTOR_SIZE);
    };

    auto WriteGroupDescriptor = [&](uint32_t GroupIndex, const uint8_t* GroupDescriptor) -> bool
    {
        uint32_t GroupDescriptorTableOffset = (BlockSizeBytes == 1024u) ? (2u * BlockSizeBytes) : BlockSizeBytes;
        uint64_t GroupDescriptorOffset64    = static_cast<uint64_t>(GroupDescriptorTableOffset) + (static_cast<uint64_t>(GroupIndex) * EXT2_GROUP_DESCRIPTOR_SIZE);
        if (GroupDescriptorOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        return WriteBytesToDisk(static_cast<uint32_t>(GroupDescriptorOffset64), GroupDescriptor, EXT2_GROUP_DESCRIPTOR_SIZE);
    };

    auto WriteUpdatedSuperblockCounters = [&]() -> bool
    {
        uint8_t SuperBlock[EXT2_SUPERBLOCK_SIZE_BYTES];
        kmemset(SuperBlock, 0, sizeof(SuperBlock));
        if (!ReadBytesFromDisk(EXT2_SUPERBLOCK_OFFSET_BYTES, SuperBlock, EXT2_SUPERBLOCK_SIZE_BYTES))
        {
            return false;
        }

        WriteLE32(&SuperBlock[EXT2_SUPERBLOCK_FREE_BLOCKS_OFFSET], FreeBlocksCount);
        WriteLE32(&SuperBlock[EXT2_SUPERBLOCK_FREE_INODES_OFFSET], FreeInodesCount);
        return WriteBytesToDisk(EXT2_SUPERBLOCK_OFFSET_BYTES, SuperBlock, EXT2_SUPERBLOCK_SIZE_BYTES);
    };

    auto WriteInodeData = [&](uint32_t InodeNumber, const uint8_t* InodeData) -> bool
    {
        if (InodeNumber == 0)
        {
            return false;
        }

        uint32_t ZeroBasedInode = InodeNumber - 1;
        uint32_t GroupIndex     = ZeroBasedInode / InodesPerGroup;
        uint32_t InodeIndex     = ZeroBasedInode % InodesPerGroup;

        uint8_t GroupDescriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
        kmemset(GroupDescriptor, 0, sizeof(GroupDescriptor));
        if (!ReadGroupDescriptor(GroupIndex, GroupDescriptor))
        {
            return false;
        }

        uint32_t InodeTableBlock = ReadLE32(&GroupDescriptor[8]);
        if (InodeTableBlock == 0)
        {
            return false;
        }

        uint64_t InodeOffset64 = (static_cast<uint64_t>(InodeTableBlock) * BlockSizeBytes) + (static_cast<uint64_t>(InodeIndex) * InodeSizeBytes);
        if (InodeOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        return WriteBytesToDisk(static_cast<uint32_t>(InodeOffset64), InodeData, InodeSizeBytes);
    };

    auto FindEntryInDirectory = [&](uint32_t DirectoryInode, const char* EntryName, uint32_t EntryNameLength, uint32_t* ChildInodeNumber, bool* IsDirectory, bool* Found) -> bool
    {
        if (Found == nullptr)
        {
            return false;
        }

        *Found = false;

        uint8_t DirectoryInodeData[256];
        kmemset(DirectoryInodeData, 0, sizeof(DirectoryInodeData));
        if (InodeSizeBytes > sizeof(DirectoryInodeData) || !ReadInode(DirectoryInode, DirectoryInodeData, sizeof(DirectoryInodeData)))
        {
            return false;
        }

        uint16_t DirectoryMode = ReadLE16(&DirectoryInodeData[0]);
        if ((DirectoryMode & EXT2_INODE_MODE_DIRECTORY) == 0)
        {
            return false;
        }

        for (uint32_t PointerIndex = 0; PointerIndex < 12; ++PointerIndex)
        {
            uint32_t BlockNumber = ReadLE32(&DirectoryInodeData[40 + (PointerIndex * 4)]);
            if (BlockNumber == 0)
            {
                continue;
            }

            uint64_t BlockOffset64 = static_cast<uint64_t>(BlockNumber) * BlockSizeBytes;
            if (BlockOffset64 > 0xFFFFFFFFu)
            {
                return false;
            }

            uint8_t* BlockData = new uint8_t[BlockSizeBytes];
            if (BlockData == nullptr)
            {
                return false;
            }

            bool ReadOk = ReadBytesFromDisk(static_cast<uint32_t>(BlockOffset64), BlockData, BlockSizeBytes);
            if (!ReadOk)
            {
                delete[] BlockData;
                return false;
            }

            uint32_t EntryOffset = 0;
            while (EntryOffset + 8 <= BlockSizeBytes)
            {
                uint32_t EntryInode  = ReadLE32(&BlockData[EntryOffset]);
                uint16_t EntryLength = ReadLE16(&BlockData[EntryOffset + 4]);
                uint8_t  NameLength  = BlockData[EntryOffset + 6];
                uint8_t  EntryType   = BlockData[EntryOffset + 7];

                if (EntryLength < 8 || (EntryOffset + EntryLength) > BlockSizeBytes)
                {
                    break;
                }

                if (EntryInode != 0 && NameLength == EntryNameLength && (8u + NameLength) <= EntryLength)
                {
                    bool Matches = true;
                    for (uint32_t NameIndex = 0; NameIndex < NameLength; ++NameIndex)
                    {
                        if (BlockData[EntryOffset + 8 + NameIndex] != static_cast<uint8_t>(EntryName[NameIndex]))
                        {
                            Matches = false;
                            break;
                        }
                    }

                    if (Matches)
                    {
                        if (ChildInodeNumber != nullptr)
                        {
                            *ChildInodeNumber = EntryInode;
                        }

                        if (IsDirectory != nullptr)
                        {
                            if (EntryType == 2)
                            {
                                *IsDirectory = true;
                            }
                            else if (EntryType == 1 || EntryType == 7)
                            {
                                *IsDirectory = false;
                            }
                            else
                            {
                                uint8_t ChildInodeData[256];
                                kmemset(ChildInodeData, 0, sizeof(ChildInodeData));
                                if (InodeSizeBytes > sizeof(ChildInodeData) || !ReadInode(EntryInode, ChildInodeData, sizeof(ChildInodeData)))
                                {
                                    delete[] BlockData;
                                    return false;
                                }

                                uint16_t ChildMode = ReadLE16(&ChildInodeData[0]);
                                *IsDirectory       = ((ChildMode & EXT2_INODE_MODE_DIRECTORY) != 0);
                            }
                        }

                        *Found = true;
                        delete[] BlockData;
                        return true;
                    }
                }

                EntryOffset += EntryLength;
            }

            delete[] BlockData;
        }

        return true;
    };

    auto AllocateInodeInGroup = [&](uint32_t GroupIndex, bool CountAsDirectory, uint32_t* AllocatedInode) -> bool
    {
        if (AllocatedInode == nullptr)
        {
            return false;
        }

        uint8_t GroupDescriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
        kmemset(GroupDescriptor, 0, sizeof(GroupDescriptor));
        if (!ReadGroupDescriptor(GroupIndex, GroupDescriptor))
        {
            return false;
        }

        uint16_t GroupFreeInodes = ReadLE16(&GroupDescriptor[14]);
        if (GroupFreeInodes == 0)
        {
            return false;
        }

        uint32_t InodeBitmapBlock = ReadLE32(&GroupDescriptor[4]);
        if (InodeBitmapBlock == 0)
        {
            return false;
        }

        uint64_t BitmapOffset64 = static_cast<uint64_t>(InodeBitmapBlock) * BlockSizeBytes;
        if (BitmapOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        uint8_t* InodeBitmap = new uint8_t[BlockSizeBytes];
        if (InodeBitmap == nullptr)
        {
            return false;
        }

        if (!ReadBytesFromDisk(static_cast<uint32_t>(BitmapOffset64), InodeBitmap, BlockSizeBytes))
        {
            delete[] InodeBitmap;
            return false;
        }

        for (uint32_t BitIndex = 0; BitIndex < InodesPerGroup; ++BitIndex)
        {
            uint32_t InodeNumber = (GroupIndex * InodesPerGroup) + BitIndex + 1;
            if (InodeNumber > InodesCount)
            {
                break;
            }

            uint32_t ByteIndex = BitIndex / 8;
            uint8_t  BitMask   = static_cast<uint8_t>(1u << (BitIndex % 8));

            if ((InodeBitmap[ByteIndex] & BitMask) == 0)
            {
                InodeBitmap[ByteIndex] = static_cast<uint8_t>(InodeBitmap[ByteIndex] | BitMask);

                if (!WriteBytesToDisk(static_cast<uint32_t>(BitmapOffset64), InodeBitmap, BlockSizeBytes))
                {
                    delete[] InodeBitmap;
                    return false;
                }

                delete[] InodeBitmap;

                WriteLE16(&GroupDescriptor[14], static_cast<uint16_t>(GroupFreeInodes - 1));
                if (CountAsDirectory)
                {
                    uint16_t UsedDirectories = ReadLE16(&GroupDescriptor[16]);
                    WriteLE16(&GroupDescriptor[16], static_cast<uint16_t>(UsedDirectories + 1));
                }

                if (!WriteGroupDescriptor(GroupIndex, GroupDescriptor))
                {
                    return false;
                }

                if (FreeInodesCount == 0)
                {
                    return false;
                }
                --FreeInodesCount;

                if (!WriteUpdatedSuperblockCounters())
                {
                    ++FreeInodesCount;
                    return false;
                }

                *AllocatedInode = InodeNumber;
                return true;
            }
        }

        delete[] InodeBitmap;
        return false;
    };

    auto AllocateBlockInGroup = [&](uint32_t GroupIndex, uint32_t* AllocatedBlock) -> bool
    {
        if (AllocatedBlock == nullptr)
        {
            return false;
        }

        uint8_t GroupDescriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
        kmemset(GroupDescriptor, 0, sizeof(GroupDescriptor));
        if (!ReadGroupDescriptor(GroupIndex, GroupDescriptor))
        {
            return false;
        }

        uint16_t GroupFreeBlocks = ReadLE16(&GroupDescriptor[12]);
        if (GroupFreeBlocks == 0)
        {
            return false;
        }

        uint32_t BlockBitmapBlock = ReadLE32(&GroupDescriptor[0]);
        if (BlockBitmapBlock == 0)
        {
            return false;
        }

        uint64_t BitmapOffset64 = static_cast<uint64_t>(BlockBitmapBlock) * BlockSizeBytes;
        if (BitmapOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        uint8_t* BlockBitmap = new uint8_t[BlockSizeBytes];
        if (BlockBitmap == nullptr)
        {
            return false;
        }

        if (!ReadBytesFromDisk(static_cast<uint32_t>(BitmapOffset64), BlockBitmap, BlockSizeBytes))
        {
            delete[] BlockBitmap;
            return false;
        }

        for (uint32_t BitIndex = 0; BitIndex < BlocksPerGroup; ++BitIndex)
        {
            uint32_t BlockNumber = (GroupIndex * BlocksPerGroup) + BitIndex + FirstDataBlock;
            if (BlockNumber >= BlocksCount)
            {
                break;
            }

            uint32_t ByteIndex = BitIndex / 8;
            uint8_t  BitMask   = static_cast<uint8_t>(1u << (BitIndex % 8));

            if ((BlockBitmap[ByteIndex] & BitMask) == 0)
            {
                BlockBitmap[ByteIndex] = static_cast<uint8_t>(BlockBitmap[ByteIndex] | BitMask);

                if (!WriteBytesToDisk(static_cast<uint32_t>(BitmapOffset64), BlockBitmap, BlockSizeBytes))
                {
                    delete[] BlockBitmap;
                    return false;
                }

                delete[] BlockBitmap;

                WriteLE16(&GroupDescriptor[12], static_cast<uint16_t>(GroupFreeBlocks - 1));
                if (!WriteGroupDescriptor(GroupIndex, GroupDescriptor))
                {
                    return false;
                }

                if (FreeBlocksCount == 0)
                {
                    return false;
                }
                --FreeBlocksCount;

                if (!WriteUpdatedSuperblockCounters())
                {
                    ++FreeBlocksCount;
                    return false;
                }

                *AllocatedBlock = BlockNumber;
                return true;
            }
        }

        delete[] BlockBitmap;
        return false;
    };

    uint32_t ParentInodeNumber = EXT2_ROOT_INODE_NUMBER;
    for (uint32_t SegmentIndex = 0; SegmentIndex + 1 < SegmentCount; ++SegmentIndex)
    {
        uint32_t ChildInode = 0;
        bool     IsDir      = false;
        bool     Found      = false;

        if (!FindEntryInDirectory(ParentInodeNumber, Segments[SegmentIndex], SegmentLengths[SegmentIndex], &ChildInode, &IsDir, &Found))
        {
            return false;
        }

        if (!Found || !IsDir)
        {
            return false;
        }

        ParentInodeNumber = ChildInode;
    }

    {
        bool Exists      = false;
        bool ExistsIsDir = false;
        if (!FindEntryInDirectory(ParentInodeNumber, NewEntryName, NewEntryNameBytes, nullptr, &ExistsIsDir, &Exists))
        {
            return false;
        }

        if (Exists)
        {
            return false;
        }
    }

    uint32_t ParentGroupIndex = (ParentInodeNumber - 1) / InodesPerGroup;

    uint32_t NewInodeNumber = 0;
    if (!AllocateInodeInGroup(ParentGroupIndex, CreatingDirectory, &NewInodeNumber))
    {
        return false;
    }

    uint32_t NewDataBlockNumber = 0;
    if (CreatingDirectory)
    {
        if (!AllocateBlockInGroup(ParentGroupIndex, &NewDataBlockNumber))
        {
            return false;
        }
    }

    if (CreatingDirectory)
    {
        uint8_t* NewDirectoryBlock = new uint8_t[BlockSizeBytes];
        if (NewDirectoryBlock == nullptr)
        {
            return false;
        }

        kmemset(NewDirectoryBlock, 0, BlockSizeBytes);

        WriteLE32(&NewDirectoryBlock[0], NewInodeNumber);
        WriteLE16(&NewDirectoryBlock[4], 12);
        NewDirectoryBlock[6] = 1;
        NewDirectoryBlock[7] = 2;
        NewDirectoryBlock[8] = '.';

        WriteLE32(&NewDirectoryBlock[12], ParentInodeNumber);
        WriteLE16(&NewDirectoryBlock[16], static_cast<uint16_t>(BlockSizeBytes - 12));
        NewDirectoryBlock[18] = 2;
        NewDirectoryBlock[19] = 2;
        NewDirectoryBlock[20] = '.';
        NewDirectoryBlock[21] = '.';

        uint64_t NewDirectoryBlockOffset64 = static_cast<uint64_t>(NewDataBlockNumber) * BlockSizeBytes;
        if (NewDirectoryBlockOffset64 > 0xFFFFFFFFu || !WriteBytesToDisk(static_cast<uint32_t>(NewDirectoryBlockOffset64), NewDirectoryBlock, BlockSizeBytes))
        {
            delete[] NewDirectoryBlock;
            return false;
        }

        delete[] NewDirectoryBlock;
    }

    uint8_t* NewInodeData = new uint8_t[InodeSizeBytes];
    if (NewInodeData == nullptr)
    {
        return false;
    }

    kmemset(NewInodeData, 0, InodeSizeBytes);
    WriteLE16(&NewInodeData[0], CreatingDirectory ? EXT2_INODE_MODE_DIRECTORY_0755 : EXT2_INODE_MODE_REGULAR_0644);
    WriteLE32(&NewInodeData[4], 0);
    WriteLE16(&NewInodeData[26], CreatingDirectory ? 2 : 1);
    WriteLE32(&NewInodeData[28], CreatingDirectory ? (BlockSizeBytes / 512u) : 0);
    WriteLE32(&NewInodeData[40], CreatingDirectory ? NewDataBlockNumber : 0);

    if (!WriteInodeData(NewInodeNumber, NewInodeData))
    {
        delete[] NewInodeData;
        return false;
    }

    delete[] NewInodeData;

    uint8_t ParentInodeData[256];
    kmemset(ParentInodeData, 0, sizeof(ParentInodeData));
    if (InodeSizeBytes > sizeof(ParentInodeData) || !ReadInode(ParentInodeNumber, ParentInodeData, sizeof(ParentInodeData)))
    {
        return false;
    }

    bool     AddedToParent        = false;
    uint16_t NewEntryRecordLength = AlignTo4(static_cast<uint16_t>(8u + NewEntryNameBytes));

    for (uint32_t PointerIndex = 0; PointerIndex < 12 && !AddedToParent; ++PointerIndex)
    {
        uint32_t ParentBlockNumber = ReadLE32(&ParentInodeData[40 + (PointerIndex * 4)]);
        if (ParentBlockNumber == 0)
        {
            continue;
        }

        uint64_t ParentBlockOffset64 = static_cast<uint64_t>(ParentBlockNumber) * BlockSizeBytes;
        if (ParentBlockOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        uint8_t* ParentBlockData = new uint8_t[BlockSizeBytes];
        if (ParentBlockData == nullptr)
        {
            return false;
        }

        if (!ReadBytesFromDisk(static_cast<uint32_t>(ParentBlockOffset64), ParentBlockData, BlockSizeBytes))
        {
            delete[] ParentBlockData;
            return false;
        }

        uint32_t EntryOffset = 0;
        while (EntryOffset + 8 <= BlockSizeBytes)
        {
            uint16_t EntryLength = ReadLE16(&ParentBlockData[EntryOffset + 4]);
            uint8_t  NameLength  = ParentBlockData[EntryOffset + 6];

            if (EntryLength < 8 || (EntryOffset + EntryLength) > BlockSizeBytes)
            {
                break;
            }

            uint16_t CurrentMinimumLength = AlignTo4(static_cast<uint16_t>(8u + NameLength));
            if (EntryLength >= static_cast<uint16_t>(CurrentMinimumLength + NewEntryRecordLength))
            {
                uint16_t NewEntryOffset  = static_cast<uint16_t>(EntryOffset + CurrentMinimumLength);
                uint16_t RemainingLength = static_cast<uint16_t>(EntryLength - CurrentMinimumLength);

                WriteLE16(&ParentBlockData[EntryOffset + 4], CurrentMinimumLength);

                WriteLE32(&ParentBlockData[NewEntryOffset], NewInodeNumber);
                WriteLE16(&ParentBlockData[NewEntryOffset + 4], RemainingLength);
                ParentBlockData[NewEntryOffset + 6] = static_cast<uint8_t>(NewEntryNameBytes);
                ParentBlockData[NewEntryOffset + 7] = CreatingDirectory ? 2 : 1;

                for (uint32_t NameIndex = 0; NameIndex < NewEntryNameBytes; ++NameIndex)
                {
                    ParentBlockData[NewEntryOffset + 8 + NameIndex] = static_cast<uint8_t>(NewEntryName[NameIndex]);
                }

                if (!WriteBytesToDisk(static_cast<uint32_t>(ParentBlockOffset64), ParentBlockData, BlockSizeBytes))
                {
                    delete[] ParentBlockData;
                    return false;
                }

                AddedToParent = true;
                delete[] ParentBlockData;
                break;
            }

            EntryOffset += EntryLength;
        }

        if (!AddedToParent)
        {
            delete[] ParentBlockData;
        }
    }

    if (!AddedToParent)
    {
        return false;
    }

    if (CreatingDirectory)
    {
        uint16_t ParentLinks = ReadLE16(&ParentInodeData[26]);
        WriteLE16(&ParentInodeData[26], static_cast<uint16_t>(ParentLinks + 1));
    }

    if (!WriteInodeData(ParentInodeNumber, ParentInodeData))
    {
        return false;
    }

    return true;
}

bool ExtendedFileSystemManager::DeleteFile(const char* Path, ExtendedFileSystemEntryType Type)
{
    bool DeletingDirectory   = (Type == ExtendedFileSystemEntryTypeDirectory);
    bool DeletingRegularFile = (Type == ExtendedFileSystemEntryTypeRegularFile);
    if (!DeletingDirectory && !DeletingRegularFile)
    {
        return false;
    }

    if (!Initialized || Path == nullptr || BlockSizeBytes == 0 || InodeSizeBytes == 0 || InodesPerGroup == 0 || BlocksPerGroup == 0)
    {
        return false;
    }

    const char* EffectivePath = Path;
    while (*EffectivePath == '/')
    {
        ++EffectivePath;
    }

    if (*EffectivePath == '\0')
    {
        return false;
    }

    constexpr uint32_t MAX_PATH_CHARS    = 1024;
    constexpr uint32_t MAX_PATH_SEGMENTS = 64;
    constexpr uint32_t MAX_SEGMENT_CHARS = 255;
    char               PathBuffer[MAX_PATH_CHARS];
    kmemset(PathBuffer, 0, sizeof(PathBuffer));

    uint32_t PathLength = 0;
    while (EffectivePath[PathLength] != '\0' && PathLength < (MAX_PATH_CHARS - 1))
    {
        PathBuffer[PathLength] = EffectivePath[PathLength];
        ++PathLength;
    }
    PathBuffer[PathLength] = '\0';

    while (PathLength > 0 && PathBuffer[PathLength - 1] == '/')
    {
        PathBuffer[PathLength - 1] = '\0';
        --PathLength;
    }

    if (PathLength == 0)
    {
        return false;
    }

    char     Segments[MAX_PATH_SEGMENTS][MAX_SEGMENT_CHARS + 1];
    uint32_t SegmentLengths[MAX_PATH_SEGMENTS];
    kmemset(Segments, 0, sizeof(Segments));
    kmemset(SegmentLengths, 0, sizeof(SegmentLengths));
    uint32_t SegmentCount = 0;

    uint32_t Cursor = 0;
    while (Cursor < PathLength)
    {
        while (Cursor < PathLength && PathBuffer[Cursor] == '/')
        {
            ++Cursor;
        }

        if (Cursor >= PathLength)
        {
            break;
        }

        if (SegmentCount >= MAX_PATH_SEGMENTS)
        {
            return false;
        }

        uint32_t SegmentStart = Cursor;
        while (Cursor < PathLength && PathBuffer[Cursor] != '/')
        {
            ++Cursor;
        }

        uint32_t SegmentLength = Cursor - SegmentStart;
        if (SegmentLength == 0 || SegmentLength > MAX_SEGMENT_CHARS)
        {
            return false;
        }

        for (uint32_t NameIndex = 0; NameIndex < SegmentLength; ++NameIndex)
        {
            Segments[SegmentCount][NameIndex] = PathBuffer[SegmentStart + NameIndex];
        }
        Segments[SegmentCount][SegmentLength] = '\0';
        SegmentLengths[SegmentCount]          = SegmentLength;
        ++SegmentCount;
    }

    if (SegmentCount == 0)
    {
        return false;
    }

    const char* TargetName      = Segments[SegmentCount - 1];
    uint32_t    TargetNameBytes = SegmentLengths[SegmentCount - 1];

    if ((TargetNameBytes == 1 && TargetName[0] == '.') || (TargetNameBytes == 2 && TargetName[0] == '.' && TargetName[1] == '.'))
    {
        return false;
    }

    auto ReadGroupDescriptor = [&](uint32_t GroupIndex, uint8_t* GroupDescriptor) -> bool
    {
        uint32_t GroupDescriptorTableOffset = (BlockSizeBytes == 1024u) ? (2u * BlockSizeBytes) : BlockSizeBytes;
        uint64_t GroupDescriptorOffset64    = static_cast<uint64_t>(GroupDescriptorTableOffset) + (static_cast<uint64_t>(GroupIndex) * EXT2_GROUP_DESCRIPTOR_SIZE);
        if (GroupDescriptorOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        return ReadBytesFromDisk(static_cast<uint32_t>(GroupDescriptorOffset64), GroupDescriptor, EXT2_GROUP_DESCRIPTOR_SIZE);
    };

    auto WriteGroupDescriptor = [&](uint32_t GroupIndex, const uint8_t* GroupDescriptor) -> bool
    {
        uint32_t GroupDescriptorTableOffset = (BlockSizeBytes == 1024u) ? (2u * BlockSizeBytes) : BlockSizeBytes;
        uint64_t GroupDescriptorOffset64    = static_cast<uint64_t>(GroupDescriptorTableOffset) + (static_cast<uint64_t>(GroupIndex) * EXT2_GROUP_DESCRIPTOR_SIZE);
        if (GroupDescriptorOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        return WriteBytesToDisk(static_cast<uint32_t>(GroupDescriptorOffset64), GroupDescriptor, EXT2_GROUP_DESCRIPTOR_SIZE);
    };

    auto WriteUpdatedSuperblockCounters = [&]() -> bool
    {
        uint8_t SuperBlock[EXT2_SUPERBLOCK_SIZE_BYTES];
        kmemset(SuperBlock, 0, sizeof(SuperBlock));
        if (!ReadBytesFromDisk(EXT2_SUPERBLOCK_OFFSET_BYTES, SuperBlock, EXT2_SUPERBLOCK_SIZE_BYTES))
        {
            return false;
        }

        WriteLE32(&SuperBlock[EXT2_SUPERBLOCK_FREE_BLOCKS_OFFSET], FreeBlocksCount);
        WriteLE32(&SuperBlock[EXT2_SUPERBLOCK_FREE_INODES_OFFSET], FreeInodesCount);
        return WriteBytesToDisk(EXT2_SUPERBLOCK_OFFSET_BYTES, SuperBlock, EXT2_SUPERBLOCK_SIZE_BYTES);
    };

    auto WriteInodeData = [&](uint32_t InodeNumber, const uint8_t* InodeData) -> bool
    {
        if (InodeNumber == 0)
        {
            return false;
        }

        uint32_t ZeroBasedInode = InodeNumber - 1;
        uint32_t GroupIndex     = ZeroBasedInode / InodesPerGroup;
        uint32_t InodeIndex     = ZeroBasedInode % InodesPerGroup;

        uint8_t GroupDescriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
        kmemset(GroupDescriptor, 0, sizeof(GroupDescriptor));
        if (!ReadGroupDescriptor(GroupIndex, GroupDescriptor))
        {
            return false;
        }

        uint32_t InodeTableBlock = ReadLE32(&GroupDescriptor[8]);
        if (InodeTableBlock == 0)
        {
            return false;
        }

        uint64_t InodeOffset64 = (static_cast<uint64_t>(InodeTableBlock) * BlockSizeBytes) + (static_cast<uint64_t>(InodeIndex) * InodeSizeBytes);
        if (InodeOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        return WriteBytesToDisk(static_cast<uint32_t>(InodeOffset64), InodeData, InodeSizeBytes);
    };

    auto FindEntryInDirectory = [&](uint32_t DirectoryInode, const char* EntryName, uint32_t EntryNameLength, uint32_t* ChildInodeNumber, bool* IsDirectory, bool* Found) -> bool
    {
        if (Found == nullptr)
        {
            return false;
        }

        *Found = false;

        uint8_t DirectoryInodeData[256];
        kmemset(DirectoryInodeData, 0, sizeof(DirectoryInodeData));
        if (InodeSizeBytes > sizeof(DirectoryInodeData) || !ReadInode(DirectoryInode, DirectoryInodeData, sizeof(DirectoryInodeData)))
        {
            return false;
        }

        uint16_t DirectoryMode = ReadLE16(&DirectoryInodeData[0]);
        if ((DirectoryMode & EXT2_INODE_MODE_DIRECTORY) == 0)
        {
            return false;
        }

        for (uint32_t PointerIndex = 0; PointerIndex < 12; ++PointerIndex)
        {
            uint32_t BlockNumber = ReadLE32(&DirectoryInodeData[40 + (PointerIndex * 4)]);
            if (BlockNumber == 0)
            {
                continue;
            }

            uint64_t BlockOffset64 = static_cast<uint64_t>(BlockNumber) * BlockSizeBytes;
            if (BlockOffset64 > 0xFFFFFFFFu)
            {
                return false;
            }

            uint8_t* BlockData = new uint8_t[BlockSizeBytes];
            if (BlockData == nullptr)
            {
                return false;
            }

            bool ReadOk = ReadBytesFromDisk(static_cast<uint32_t>(BlockOffset64), BlockData, BlockSizeBytes);
            if (!ReadOk)
            {
                delete[] BlockData;
                return false;
            }

            uint32_t EntryOffset = 0;
            while (EntryOffset + 8 <= BlockSizeBytes)
            {
                uint32_t EntryInode  = ReadLE32(&BlockData[EntryOffset]);
                uint16_t EntryLength = ReadLE16(&BlockData[EntryOffset + 4]);
                uint8_t  NameLength  = BlockData[EntryOffset + 6];
                uint8_t  EntryType   = BlockData[EntryOffset + 7];

                if (EntryLength < 8 || (EntryOffset + EntryLength) > BlockSizeBytes)
                {
                    break;
                }

                if (EntryInode != 0 && NameLength == EntryNameLength && (8u + NameLength) <= EntryLength)
                {
                    bool Matches = true;
                    for (uint32_t NameIndex = 0; NameIndex < NameLength; ++NameIndex)
                    {
                        if (BlockData[EntryOffset + 8 + NameIndex] != static_cast<uint8_t>(EntryName[NameIndex]))
                        {
                            Matches = false;
                            break;
                        }
                    }

                    if (Matches)
                    {
                        if (ChildInodeNumber != nullptr)
                        {
                            *ChildInodeNumber = EntryInode;
                        }

                        if (IsDirectory != nullptr)
                        {
                            if (EntryType == 2)
                            {
                                *IsDirectory = true;
                            }
                            else if (EntryType == 1 || EntryType == 7)
                            {
                                *IsDirectory = false;
                            }
                            else
                            {
                                uint8_t ChildInodeData[256];
                                kmemset(ChildInodeData, 0, sizeof(ChildInodeData));
                                if (InodeSizeBytes > sizeof(ChildInodeData) || !ReadInode(EntryInode, ChildInodeData, sizeof(ChildInodeData)))
                                {
                                    delete[] BlockData;
                                    return false;
                                }

                                uint16_t ChildMode = ReadLE16(&ChildInodeData[0]);
                                *IsDirectory       = ((ChildMode & EXT2_INODE_MODE_DIRECTORY) != 0);
                            }
                        }

                        *Found = true;
                        delete[] BlockData;
                        return true;
                    }
                }

                EntryOffset += EntryLength;
            }

            delete[] BlockData;
        }

        return true;
    };

    auto FreeBlock = [&](uint32_t BlockNumber) -> bool
    {
        if (BlockNumber < FirstDataBlock || BlockNumber >= BlocksCount)
        {
            return false;
        }

        uint32_t RelativeBlock = BlockNumber - FirstDataBlock;
        uint32_t GroupIndex    = RelativeBlock / BlocksPerGroup;
        uint32_t BlockIndex    = RelativeBlock % BlocksPerGroup;

        uint8_t GroupDescriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
        kmemset(GroupDescriptor, 0, sizeof(GroupDescriptor));
        if (!ReadGroupDescriptor(GroupIndex, GroupDescriptor))
        {
            return false;
        }

        uint32_t BlockBitmapBlock = ReadLE32(&GroupDescriptor[0]);
        if (BlockBitmapBlock == 0)
        {
            return false;
        }

        uint64_t BitmapOffset64 = static_cast<uint64_t>(BlockBitmapBlock) * BlockSizeBytes;
        if (BitmapOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        uint8_t* BlockBitmap = new uint8_t[BlockSizeBytes];
        if (BlockBitmap == nullptr)
        {
            return false;
        }

        if (!ReadBytesFromDisk(static_cast<uint32_t>(BitmapOffset64), BlockBitmap, BlockSizeBytes))
        {
            delete[] BlockBitmap;
            return false;
        }

        uint32_t ByteIndex = BlockIndex / 8;
        uint8_t  BitMask   = static_cast<uint8_t>(1u << (BlockIndex % 8));
        if (ByteIndex >= BlockSizeBytes || (BlockBitmap[ByteIndex] & BitMask) == 0)
        {
            delete[] BlockBitmap;
            return false;
        }

        BlockBitmap[ByteIndex] = static_cast<uint8_t>(BlockBitmap[ByteIndex] & static_cast<uint8_t>(~BitMask));

        if (!WriteBytesToDisk(static_cast<uint32_t>(BitmapOffset64), BlockBitmap, BlockSizeBytes))
        {
            delete[] BlockBitmap;
            return false;
        }

        delete[] BlockBitmap;

        uint16_t GroupFreeBlocks = ReadLE16(&GroupDescriptor[12]);
        WriteLE16(&GroupDescriptor[12], static_cast<uint16_t>(GroupFreeBlocks + 1));
        if (!WriteGroupDescriptor(GroupIndex, GroupDescriptor))
        {
            return false;
        }

        ++FreeBlocksCount;
        return WriteUpdatedSuperblockCounters();
    };

    auto FreeInode = [&](uint32_t InodeNumber, bool IsDirectory) -> bool
    {
        if (InodeNumber == 0 || InodeNumber > InodesCount)
        {
            return false;
        }

        uint32_t ZeroBasedInode = InodeNumber - 1;
        uint32_t GroupIndex     = ZeroBasedInode / InodesPerGroup;
        uint32_t InodeIndex     = ZeroBasedInode % InodesPerGroup;

        uint8_t GroupDescriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
        kmemset(GroupDescriptor, 0, sizeof(GroupDescriptor));
        if (!ReadGroupDescriptor(GroupIndex, GroupDescriptor))
        {
            return false;
        }

        uint32_t InodeBitmapBlock = ReadLE32(&GroupDescriptor[4]);
        if (InodeBitmapBlock == 0)
        {
            return false;
        }

        uint64_t BitmapOffset64 = static_cast<uint64_t>(InodeBitmapBlock) * BlockSizeBytes;
        if (BitmapOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        uint8_t* InodeBitmap = new uint8_t[BlockSizeBytes];
        if (InodeBitmap == nullptr)
        {
            return false;
        }

        if (!ReadBytesFromDisk(static_cast<uint32_t>(BitmapOffset64), InodeBitmap, BlockSizeBytes))
        {
            delete[] InodeBitmap;
            return false;
        }

        uint32_t ByteIndex = InodeIndex / 8;
        uint8_t  BitMask   = static_cast<uint8_t>(1u << (InodeIndex % 8));
        if (ByteIndex >= BlockSizeBytes || (InodeBitmap[ByteIndex] & BitMask) == 0)
        {
            delete[] InodeBitmap;
            return false;
        }

        InodeBitmap[ByteIndex] = static_cast<uint8_t>(InodeBitmap[ByteIndex] & static_cast<uint8_t>(~BitMask));

        if (!WriteBytesToDisk(static_cast<uint32_t>(BitmapOffset64), InodeBitmap, BlockSizeBytes))
        {
            delete[] InodeBitmap;
            return false;
        }

        delete[] InodeBitmap;

        uint16_t GroupFreeInodes = ReadLE16(&GroupDescriptor[14]);
        WriteLE16(&GroupDescriptor[14], static_cast<uint16_t>(GroupFreeInodes + 1));

        if (IsDirectory)
        {
            uint16_t UsedDirectories = ReadLE16(&GroupDescriptor[16]);
            if (UsedDirectories == 0)
            {
                return false;
            }
            WriteLE16(&GroupDescriptor[16], static_cast<uint16_t>(UsedDirectories - 1));
        }

        if (!WriteGroupDescriptor(GroupIndex, GroupDescriptor))
        {
            return false;
        }

        ++FreeInodesCount;
        return WriteUpdatedSuperblockCounters();
    };

    uint32_t ParentInodeNumber = EXT2_ROOT_INODE_NUMBER;
    for (uint32_t SegmentIndex = 0; SegmentIndex + 1 < SegmentCount; ++SegmentIndex)
    {
        uint32_t ChildInode = 0;
        bool     IsDir      = false;
        bool     Found      = false;

        if (!FindEntryInDirectory(ParentInodeNumber, Segments[SegmentIndex], SegmentLengths[SegmentIndex], &ChildInode, &IsDir, &Found))
        {
            return false;
        }

        if (!Found || !IsDir)
        {
            return false;
        }

        ParentInodeNumber = ChildInode;
    }

    uint32_t TargetInodeNumber  = 0;
    bool     TargetIsDirectory  = false;
    bool     TargetFoundInEntry = false;
    if (!FindEntryInDirectory(ParentInodeNumber, TargetName, TargetNameBytes, &TargetInodeNumber, &TargetIsDirectory, &TargetFoundInEntry))
    {
        return false;
    }

    if (!TargetFoundInEntry || TargetInodeNumber == 0)
    {
        return false;
    }

    if ((DeletingDirectory && !TargetIsDirectory) || (DeletingRegularFile && TargetIsDirectory))
    {
        return false;
    }

    uint8_t TargetInodeData[256];
    kmemset(TargetInodeData, 0, sizeof(TargetInodeData));
    if (InodeSizeBytes > sizeof(TargetInodeData) || !ReadInode(TargetInodeNumber, TargetInodeData, sizeof(TargetInodeData)))
    {
        return false;
    }

    uint16_t TargetMode = ReadLE16(&TargetInodeData[0]);
    uint16_t TargetType = static_cast<uint16_t>(TargetMode & EXT2_INODE_MODE_TYPE_MASK);
    if (DeletingDirectory && TargetType != EXT2_INODE_MODE_DIRECTORY)
    {
        return false;
    }

    if (DeletingRegularFile && TargetType != EXT2_INODE_MODE_REGULAR_FILE)
    {
        return false;
    }

    if (DeletingDirectory)
    {
        for (uint32_t PointerIndex = 0; PointerIndex < 12; ++PointerIndex)
        {
            uint32_t BlockNumber = ReadLE32(&TargetInodeData[40 + (PointerIndex * 4)]);
            if (BlockNumber == 0)
            {
                continue;
            }

            uint64_t BlockOffset64 = static_cast<uint64_t>(BlockNumber) * BlockSizeBytes;
            if (BlockOffset64 > 0xFFFFFFFFu)
            {
                return false;
            }

            uint8_t* BlockData = new uint8_t[BlockSizeBytes];
            if (BlockData == nullptr)
            {
                return false;
            }

            if (!ReadBytesFromDisk(static_cast<uint32_t>(BlockOffset64), BlockData, BlockSizeBytes))
            {
                delete[] BlockData;
                return false;
            }

            uint32_t EntryOffset = 0;
            while (EntryOffset + 8 <= BlockSizeBytes)
            {
                uint32_t EntryInode  = ReadLE32(&BlockData[EntryOffset]);
                uint16_t EntryLength = ReadLE16(&BlockData[EntryOffset + 4]);
                uint8_t  NameLength  = BlockData[EntryOffset + 6];

                if (EntryLength < 8 || (EntryOffset + EntryLength) > BlockSizeBytes)
                {
                    break;
                }

                bool IsDot    = (NameLength == 1 && BlockData[EntryOffset + 8] == '.');
                bool IsDotDot = (NameLength == 2 && BlockData[EntryOffset + 8] == '.' && BlockData[EntryOffset + 9] == '.');

                if (EntryInode != 0 && !IsDot && !IsDotDot)
                {
                    delete[] BlockData;
                    return false;
                }

                EntryOffset += EntryLength;
            }

            delete[] BlockData;
        }
    }

    uint8_t ParentInodeData[256];
    kmemset(ParentInodeData, 0, sizeof(ParentInodeData));
    if (InodeSizeBytes > sizeof(ParentInodeData) || !ReadInode(ParentInodeNumber, ParentInodeData, sizeof(ParentInodeData)))
    {
        return false;
    }

    bool RemovedFromParent = false;
    for (uint32_t PointerIndex = 0; PointerIndex < 12 && !RemovedFromParent; ++PointerIndex)
    {
        uint32_t ParentBlockNumber = ReadLE32(&ParentInodeData[40 + (PointerIndex * 4)]);
        if (ParentBlockNumber == 0)
        {
            continue;
        }

        uint64_t ParentBlockOffset64 = static_cast<uint64_t>(ParentBlockNumber) * BlockSizeBytes;
        if (ParentBlockOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        uint8_t* ParentBlockData = new uint8_t[BlockSizeBytes];
        if (ParentBlockData == nullptr)
        {
            return false;
        }

        if (!ReadBytesFromDisk(static_cast<uint32_t>(ParentBlockOffset64), ParentBlockData, BlockSizeBytes))
        {
            delete[] ParentBlockData;
            return false;
        }

        uint32_t EntryOffset      = 0;
        uint32_t PreviousOffset   = 0;
        bool     HasPreviousEntry = false;

        while (EntryOffset + 8 <= BlockSizeBytes)
        {
            uint32_t EntryInode  = ReadLE32(&ParentBlockData[EntryOffset]);
            uint16_t EntryLength = ReadLE16(&ParentBlockData[EntryOffset + 4]);
            uint8_t  NameLength  = ParentBlockData[EntryOffset + 6];

            if (EntryLength < 8 || (EntryOffset + EntryLength) > BlockSizeBytes)
            {
                break;
            }

            bool MatchesName = (EntryInode == TargetInodeNumber && NameLength == TargetNameBytes && (8u + NameLength) <= EntryLength);
            if (MatchesName)
            {
                for (uint32_t NameIndex = 0; NameIndex < NameLength; ++NameIndex)
                {
                    if (ParentBlockData[EntryOffset + 8 + NameIndex] != static_cast<uint8_t>(TargetName[NameIndex]))
                    {
                        MatchesName = false;
                        break;
                    }
                }
            }

            if (MatchesName)
            {
                if (HasPreviousEntry)
                {
                    uint16_t PreviousLength = ReadLE16(&ParentBlockData[PreviousOffset + 4]);
                    WriteLE16(&ParentBlockData[PreviousOffset + 4], static_cast<uint16_t>(PreviousLength + EntryLength));
                }
                else
                {
                    WriteLE32(&ParentBlockData[EntryOffset], 0);
                }

                if (!WriteBytesToDisk(static_cast<uint32_t>(ParentBlockOffset64), ParentBlockData, BlockSizeBytes))
                {
                    delete[] ParentBlockData;
                    return false;
                }

                RemovedFromParent = true;
                delete[] ParentBlockData;
                break;
            }

            HasPreviousEntry = true;
            PreviousOffset   = EntryOffset;
            EntryOffset += EntryLength;
        }

        if (!RemovedFromParent)
        {
            delete[] ParentBlockData;
        }
    }

    if (!RemovedFromParent)
    {
        return false;
    }

    if (DeletingDirectory)
    {
        uint16_t ParentLinks = ReadLE16(&ParentInodeData[26]);
        if (ParentLinks == 0)
        {
            return false;
        }

        WriteLE16(&ParentInodeData[26], static_cast<uint16_t>(ParentLinks - 1));
        if (!WriteInodeData(ParentInodeNumber, ParentInodeData))
        {
            return false;
        }
    }

    for (uint32_t PointerIndex = 0; PointerIndex < 12; ++PointerIndex)
    {
        uint32_t BlockNumber = ReadLE32(&TargetInodeData[40 + (PointerIndex * 4)]);
        if (BlockNumber != 0)
        {
            if (!FreeBlock(BlockNumber))
            {
                return false;
            }

            WriteLE32(&TargetInodeData[40 + (PointerIndex * 4)], 0);
        }
    }

    uint32_t SingleIndirectBlock = ReadLE32(&TargetInodeData[88]);
    if (SingleIndirectBlock != 0)
    {
        uint64_t SingleIndirectOffset64 = static_cast<uint64_t>(SingleIndirectBlock) * BlockSizeBytes;
        if (SingleIndirectOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        uint8_t* SingleIndirectData = new uint8_t[BlockSizeBytes];
        if (SingleIndirectData == nullptr)
        {
            return false;
        }

        if (!ReadBytesFromDisk(static_cast<uint32_t>(SingleIndirectOffset64), SingleIndirectData, BlockSizeBytes))
        {
            delete[] SingleIndirectData;
            return false;
        }

        uint32_t PointersPerBlock = BlockSizeBytes / 4;
        for (uint32_t PointerIndex = 0; PointerIndex < PointersPerBlock; ++PointerIndex)
        {
            uint32_t BlockNumber = ReadLE32(&SingleIndirectData[PointerIndex * 4]);
            if (BlockNumber != 0 && !FreeBlock(BlockNumber))
            {
                delete[] SingleIndirectData;
                return false;
            }
        }

        delete[] SingleIndirectData;

        if (!FreeBlock(SingleIndirectBlock))
        {
            return false;
        }

        WriteLE32(&TargetInodeData[88], 0);
    }

    if (ReadLE32(&TargetInodeData[92]) != 0 || ReadLE32(&TargetInodeData[96]) != 0)
    {
        return false;
    }

    uint8_t* ClearedInodeData = new uint8_t[InodeSizeBytes];
    if (ClearedInodeData == nullptr)
    {
        return false;
    }

    kmemset(ClearedInodeData, 0, InodeSizeBytes);
    if (!WriteInodeData(TargetInodeNumber, ClearedInodeData))
    {
        delete[] ClearedInodeData;
        return false;
    }

    delete[] ClearedInodeData;

    if (!FreeInode(TargetInodeNumber, DeletingDirectory))
    {
        return false;
    }

    return true;
}

bool ExtendedFileSystemManager::RenameFile(const char* OldPath, const char* NewPath, ExtendedFileSystemEntryType Type)
{
    bool RenamingDirectory   = (Type == ExtendedFileSystemEntryTypeDirectory);
    bool RenamingRegularFile = (Type == ExtendedFileSystemEntryTypeRegularFile);
    if (!RenamingDirectory && !RenamingRegularFile)
    {
        return false;
    }

    if (!Initialized || OldPath == nullptr || NewPath == nullptr || BlockSizeBytes == 0 || InodeSizeBytes == 0 || InodesPerGroup == 0 || BlocksPerGroup == 0)
    {
        return false;
    }

    constexpr uint32_t MAX_PATH_CHARS    = 1024;
    constexpr uint32_t MAX_PATH_SEGMENTS = 64;
    constexpr uint32_t MAX_SEGMENT_CHARS = 255;

    auto ParsePath = [&](const char* InputPath, char Segments[][MAX_SEGMENT_CHARS + 1], uint32_t SegmentLengths[], uint32_t* SegmentCount) -> bool
    {
        if (InputPath == nullptr || SegmentCount == nullptr)
        {
            return false;
        }

        const char* EffectivePath = InputPath;
        while (*EffectivePath == '/')
        {
            ++EffectivePath;
        }

        if (*EffectivePath == '\0')
        {
            return false;
        }

        char PathBuffer[MAX_PATH_CHARS];
        kmemset(PathBuffer, 0, sizeof(PathBuffer));

        uint32_t PathLength = 0;
        while (EffectivePath[PathLength] != '\0' && PathLength < (MAX_PATH_CHARS - 1))
        {
            PathBuffer[PathLength] = EffectivePath[PathLength];
            ++PathLength;
        }
        PathBuffer[PathLength] = '\0';

        while (PathLength > 0 && PathBuffer[PathLength - 1] == '/')
        {
            PathBuffer[PathLength - 1] = '\0';
            --PathLength;
        }

        if (PathLength == 0)
        {
            return false;
        }

        kmemset(Segments, 0, sizeof(char) * MAX_PATH_SEGMENTS * (MAX_SEGMENT_CHARS + 1));
        kmemset(SegmentLengths, 0, sizeof(uint32_t) * MAX_PATH_SEGMENTS);
        *SegmentCount = 0;

        uint32_t Cursor = 0;
        while (Cursor < PathLength)
        {
            while (Cursor < PathLength && PathBuffer[Cursor] == '/')
            {
                ++Cursor;
            }

            if (Cursor >= PathLength)
            {
                break;
            }

            if (*SegmentCount >= MAX_PATH_SEGMENTS)
            {
                return false;
            }

            uint32_t SegmentStart = Cursor;
            while (Cursor < PathLength && PathBuffer[Cursor] != '/')
            {
                ++Cursor;
            }

            uint32_t SegmentLength = Cursor - SegmentStart;
            if (SegmentLength == 0 || SegmentLength > MAX_SEGMENT_CHARS)
            {
                return false;
            }

            for (uint32_t NameIndex = 0; NameIndex < SegmentLength; ++NameIndex)
            {
                Segments[*SegmentCount][NameIndex] = PathBuffer[SegmentStart + NameIndex];
            }
            Segments[*SegmentCount][SegmentLength] = '\0';
            SegmentLengths[*SegmentCount]          = SegmentLength;
            ++(*SegmentCount);
        }

        if (*SegmentCount == 0)
        {
            return false;
        }

        uint32_t LastSegmentIndex  = *SegmentCount - 1;
        uint32_t LastSegmentLength = SegmentLengths[LastSegmentIndex];
        const char* LastSegment    = Segments[LastSegmentIndex];

        if ((LastSegmentLength == 1 && LastSegment[0] == '.') || (LastSegmentLength == 2 && LastSegment[0] == '.' && LastSegment[1] == '.'))
        {
            return false;
        }

        return true;
    };

    auto ReadInodeData = [&](uint32_t InodeNumber, uint8_t* InodeData) -> bool
    {
        if (InodeData == nullptr || InodeSizeBytes > 256)
        {
            return false;
        }

        kmemset(InodeData, 0, 256);
        return ReadInode(InodeNumber, InodeData, 256);
    };

    auto WriteInodeData = [&](uint32_t InodeNumber, const uint8_t* InodeData) -> bool
    {
        if (InodeNumber == 0 || InodeData == nullptr)
        {
            return false;
        }

        uint32_t ZeroBasedInode = InodeNumber - 1;
        uint32_t GroupIndex     = ZeroBasedInode / InodesPerGroup;
        uint32_t InodeIndex     = ZeroBasedInode % InodesPerGroup;

        uint32_t GroupDescriptorTableOffset = (BlockSizeBytes == 1024u) ? (2u * BlockSizeBytes) : BlockSizeBytes;
        uint64_t GroupDescriptorOffset64    = static_cast<uint64_t>(GroupDescriptorTableOffset) + (static_cast<uint64_t>(GroupIndex) * EXT2_GROUP_DESCRIPTOR_SIZE);
        if (GroupDescriptorOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        uint8_t GroupDescriptor[EXT2_GROUP_DESCRIPTOR_SIZE];
        kmemset(GroupDescriptor, 0, sizeof(GroupDescriptor));
        if (!ReadBytesFromDisk(static_cast<uint32_t>(GroupDescriptorOffset64), GroupDescriptor, EXT2_GROUP_DESCRIPTOR_SIZE))
        {
            return false;
        }

        uint32_t InodeTableBlock = ReadLE32(&GroupDescriptor[8]);
        if (InodeTableBlock == 0)
        {
            return false;
        }

        uint64_t InodeOffset64 = (static_cast<uint64_t>(InodeTableBlock) * BlockSizeBytes) + (static_cast<uint64_t>(InodeIndex) * InodeSizeBytes);
        if (InodeOffset64 > 0xFFFFFFFFu)
        {
            return false;
        }

        return WriteBytesToDisk(static_cast<uint32_t>(InodeOffset64), InodeData, InodeSizeBytes);
    };

    auto FindEntryInDirectory = [&](uint32_t DirectoryInode, const char* EntryName, uint32_t EntryNameLength, uint32_t* ChildInodeNumber, bool* IsDirectory, bool* Found) -> bool
    {
        if (Found == nullptr)
        {
            return false;
        }

        *Found = false;

        uint8_t DirectoryInodeData[256];
        if (!ReadInodeData(DirectoryInode, DirectoryInodeData))
        {
            return false;
        }

        uint16_t DirectoryMode = ReadLE16(&DirectoryInodeData[0]);
        if ((DirectoryMode & EXT2_INODE_MODE_DIRECTORY) == 0)
        {
            return false;
        }

        for (uint32_t PointerIndex = 0; PointerIndex < 12; ++PointerIndex)
        {
            uint32_t BlockNumber = ReadLE32(&DirectoryInodeData[40 + (PointerIndex * 4)]);
            if (BlockNumber == 0)
            {
                continue;
            }

            uint64_t BlockOffset64 = static_cast<uint64_t>(BlockNumber) * BlockSizeBytes;
            if (BlockOffset64 > 0xFFFFFFFFu)
            {
                return false;
            }

            uint8_t* BlockData = new uint8_t[BlockSizeBytes];
            if (BlockData == nullptr)
            {
                return false;
            }

            if (!ReadBytesFromDisk(static_cast<uint32_t>(BlockOffset64), BlockData, BlockSizeBytes))
            {
                delete[] BlockData;
                return false;
            }

            uint32_t EntryOffset = 0;
            while (EntryOffset + 8 <= BlockSizeBytes)
            {
                uint32_t EntryInode  = ReadLE32(&BlockData[EntryOffset]);
                uint16_t EntryLength = ReadLE16(&BlockData[EntryOffset + 4]);
                uint8_t  NameLength  = BlockData[EntryOffset + 6];
                uint8_t  EntryType   = BlockData[EntryOffset + 7];

                if (EntryLength < 8 || (EntryOffset + EntryLength) > BlockSizeBytes)
                {
                    break;
                }

                if (EntryInode != 0 && NameLength == EntryNameLength && (8u + NameLength) <= EntryLength)
                {
                    bool Matches = true;
                    for (uint32_t NameIndex = 0; NameIndex < NameLength; ++NameIndex)
                    {
                        if (BlockData[EntryOffset + 8 + NameIndex] != static_cast<uint8_t>(EntryName[NameIndex]))
                        {
                            Matches = false;
                            break;
                        }
                    }

                    if (Matches)
                    {
                        if (ChildInodeNumber != nullptr)
                        {
                            *ChildInodeNumber = EntryInode;
                        }

                        if (IsDirectory != nullptr)
                        {
                            if (EntryType == 2)
                            {
                                *IsDirectory = true;
                            }
                            else if (EntryType == 1 || EntryType == 7)
                            {
                                *IsDirectory = false;
                            }
                            else
                            {
                                uint8_t ChildInodeData[256];
                                if (!ReadInodeData(EntryInode, ChildInodeData))
                                {
                                    delete[] BlockData;
                                    return false;
                                }

                                uint16_t ChildMode = ReadLE16(&ChildInodeData[0]);
                                *IsDirectory       = ((ChildMode & EXT2_INODE_MODE_DIRECTORY) != 0);
                            }
                        }

                        *Found = true;
                        delete[] BlockData;
                        return true;
                    }
                }

                EntryOffset += EntryLength;
            }

            delete[] BlockData;
        }

        return true;
    };

    auto ResolveParentInode = [&](char Segments[][MAX_SEGMENT_CHARS + 1], uint32_t SegmentLengths[], uint32_t SegmentCount, uint32_t* ParentInode) -> bool
    {
        if (ParentInode == nullptr)
        {
            return false;
        }

        uint32_t CurrentInode = EXT2_ROOT_INODE_NUMBER;
        for (uint32_t SegmentIndex = 0; SegmentIndex + 1 < SegmentCount; ++SegmentIndex)
        {
            uint32_t ChildInode = 0;
            bool     IsDir      = false;
            bool     Found      = false;

            if (!FindEntryInDirectory(CurrentInode, Segments[SegmentIndex], SegmentLengths[SegmentIndex], &ChildInode, &IsDir, &Found))
            {
                return false;
            }

            if (!Found || !IsDir)
            {
                return false;
            }

            CurrentInode = ChildInode;
        }

        *ParentInode = CurrentInode;
        return true;
    };

    auto AddDirectoryEntry = [&](uint32_t ParentInodeNumber, const char* EntryName, uint32_t EntryNameLength, uint32_t EntryInode, uint8_t EntryType) -> bool
    {
        if (EntryName == nullptr || EntryNameLength == 0 || EntryNameLength > 255 || EntryInode == 0)
        {
            return false;
        }

        uint8_t ParentInodeData[256];
        if (!ReadInodeData(ParentInodeNumber, ParentInodeData))
        {
            return false;
        }

        uint16_t EntryRecordLength = AlignTo4(static_cast<uint16_t>(8u + EntryNameLength));
        for (uint32_t PointerIndex = 0; PointerIndex < 12; ++PointerIndex)
        {
            uint32_t ParentBlockNumber = ReadLE32(&ParentInodeData[40 + (PointerIndex * 4)]);
            if (ParentBlockNumber == 0)
            {
                continue;
            }

            uint64_t ParentBlockOffset64 = static_cast<uint64_t>(ParentBlockNumber) * BlockSizeBytes;
            if (ParentBlockOffset64 > 0xFFFFFFFFu)
            {
                return false;
            }

            uint8_t* ParentBlockData = new uint8_t[BlockSizeBytes];
            if (ParentBlockData == nullptr)
            {
                return false;
            }

            if (!ReadBytesFromDisk(static_cast<uint32_t>(ParentBlockOffset64), ParentBlockData, BlockSizeBytes))
            {
                delete[] ParentBlockData;
                return false;
            }

            uint32_t EntryOffset = 0;
            while (EntryOffset + 8 <= BlockSizeBytes)
            {
                uint16_t ExistingLength = ReadLE16(&ParentBlockData[EntryOffset + 4]);
                uint8_t  ExistingName   = ParentBlockData[EntryOffset + 6];

                if (ExistingLength < 8 || (EntryOffset + ExistingLength) > BlockSizeBytes)
                {
                    break;
                }

                uint16_t MinimalLength = AlignTo4(static_cast<uint16_t>(8u + ExistingName));
                if (ExistingLength >= static_cast<uint16_t>(MinimalLength + EntryRecordLength))
                {
                    uint16_t NewEntryOffset  = static_cast<uint16_t>(EntryOffset + MinimalLength);
                    uint16_t RemainingLength = static_cast<uint16_t>(ExistingLength - MinimalLength);

                    WriteLE16(&ParentBlockData[EntryOffset + 4], MinimalLength);
                    WriteLE32(&ParentBlockData[NewEntryOffset], EntryInode);
                    WriteLE16(&ParentBlockData[NewEntryOffset + 4], RemainingLength);
                    ParentBlockData[NewEntryOffset + 6] = static_cast<uint8_t>(EntryNameLength);
                    ParentBlockData[NewEntryOffset + 7] = EntryType;

                    for (uint32_t NameIndex = 0; NameIndex < EntryNameLength; ++NameIndex)
                    {
                        ParentBlockData[NewEntryOffset + 8 + NameIndex] = static_cast<uint8_t>(EntryName[NameIndex]);
                    }

                    for (uint32_t NameIndex = EntryNameLength; (8u + NameIndex) < RemainingLength; ++NameIndex)
                    {
                        ParentBlockData[NewEntryOffset + 8 + NameIndex] = 0;
                    }

                    bool WriteOk = WriteBytesToDisk(static_cast<uint32_t>(ParentBlockOffset64), ParentBlockData, BlockSizeBytes);
                    delete[] ParentBlockData;
                    return WriteOk;
                }

                EntryOffset += ExistingLength;
            }

            delete[] ParentBlockData;
        }

        return false;
    };

    auto RemoveDirectoryEntry = [&](uint32_t ParentInodeNumber, const char* EntryName, uint32_t EntryNameLength, uint32_t EntryInodeNumber) -> bool
    {
        uint8_t ParentInodeData[256];
        if (!ReadInodeData(ParentInodeNumber, ParentInodeData))
        {
            return false;
        }

        for (uint32_t PointerIndex = 0; PointerIndex < 12; ++PointerIndex)
        {
            uint32_t ParentBlockNumber = ReadLE32(&ParentInodeData[40 + (PointerIndex * 4)]);
            if (ParentBlockNumber == 0)
            {
                continue;
            }

            uint64_t ParentBlockOffset64 = static_cast<uint64_t>(ParentBlockNumber) * BlockSizeBytes;
            if (ParentBlockOffset64 > 0xFFFFFFFFu)
            {
                return false;
            }

            uint8_t* ParentBlockData = new uint8_t[BlockSizeBytes];
            if (ParentBlockData == nullptr)
            {
                return false;
            }

            if (!ReadBytesFromDisk(static_cast<uint32_t>(ParentBlockOffset64), ParentBlockData, BlockSizeBytes))
            {
                delete[] ParentBlockData;
                return false;
            }

            uint32_t EntryOffset      = 0;
            uint32_t PreviousOffset   = 0;
            bool     HasPreviousEntry = false;

            while (EntryOffset + 8 <= BlockSizeBytes)
            {
                uint32_t EntryInode  = ReadLE32(&ParentBlockData[EntryOffset]);
                uint16_t EntryLength = ReadLE16(&ParentBlockData[EntryOffset + 4]);
                uint8_t  NameLength  = ParentBlockData[EntryOffset + 6];

                if (EntryLength < 8 || (EntryOffset + EntryLength) > BlockSizeBytes)
                {
                    break;
                }

                bool MatchesName = (EntryInode == EntryInodeNumber && NameLength == EntryNameLength && (8u + NameLength) <= EntryLength);
                if (MatchesName)
                {
                    for (uint32_t NameIndex = 0; NameIndex < NameLength; ++NameIndex)
                    {
                        if (ParentBlockData[EntryOffset + 8 + NameIndex] != static_cast<uint8_t>(EntryName[NameIndex]))
                        {
                            MatchesName = false;
                            break;
                        }
                    }
                }

                if (MatchesName)
                {
                    if (HasPreviousEntry)
                    {
                        uint16_t PreviousLength = ReadLE16(&ParentBlockData[PreviousOffset + 4]);
                        WriteLE16(&ParentBlockData[PreviousOffset + 4], static_cast<uint16_t>(PreviousLength + EntryLength));
                    }
                    else
                    {
                        WriteLE32(&ParentBlockData[EntryOffset], 0);
                    }

                    bool WriteOk = WriteBytesToDisk(static_cast<uint32_t>(ParentBlockOffset64), ParentBlockData, BlockSizeBytes);
                    delete[] ParentBlockData;
                    return WriteOk;
                }

                HasPreviousEntry = true;
                PreviousOffset   = EntryOffset;
                EntryOffset += EntryLength;
            }

            delete[] ParentBlockData;
        }

        return false;
    };

    auto GetParentDirectoryInode = [&](uint32_t DirectoryInodeNumber, uint32_t* ParentInodeOut) -> bool
    {
        if (ParentInodeOut == nullptr)
        {
            return false;
        }

        uint8_t DirectoryInodeData[256];
        if (!ReadInodeData(DirectoryInodeNumber, DirectoryInodeData))
        {
            return false;
        }

        uint16_t DirectoryMode = ReadLE16(&DirectoryInodeData[0]);
        if ((DirectoryMode & EXT2_INODE_MODE_DIRECTORY) == 0)
        {
            return false;
        }

        for (uint32_t PointerIndex = 0; PointerIndex < 12; ++PointerIndex)
        {
            uint32_t BlockNumber = ReadLE32(&DirectoryInodeData[40 + (PointerIndex * 4)]);
            if (BlockNumber == 0)
            {
                continue;
            }

            uint64_t BlockOffset64 = static_cast<uint64_t>(BlockNumber) * BlockSizeBytes;
            if (BlockOffset64 > 0xFFFFFFFFu)
            {
                return false;
            }

            uint8_t* BlockData = new uint8_t[BlockSizeBytes];
            if (BlockData == nullptr)
            {
                return false;
            }

            if (!ReadBytesFromDisk(static_cast<uint32_t>(BlockOffset64), BlockData, BlockSizeBytes))
            {
                delete[] BlockData;
                return false;
            }

            uint32_t EntryOffset = 0;
            while (EntryOffset + 8 <= BlockSizeBytes)
            {
                uint32_t EntryInode  = ReadLE32(&BlockData[EntryOffset]);
                uint16_t EntryLength = ReadLE16(&BlockData[EntryOffset + 4]);
                uint8_t  NameLength  = BlockData[EntryOffset + 6];

                if (EntryLength < 8 || (EntryOffset + EntryLength) > BlockSizeBytes)
                {
                    break;
                }

                if (EntryInode != 0 && NameLength == 2 && BlockData[EntryOffset + 8] == '.' && BlockData[EntryOffset + 9] == '.')
                {
                    *ParentInodeOut = EntryInode;
                    delete[] BlockData;
                    return true;
                }

                EntryOffset += EntryLength;
            }

            delete[] BlockData;
        }

        return false;
    };

    auto UpdateDotDotEntry = [&](uint32_t DirectoryInodeNumber, uint32_t NewParentInodeNumber) -> bool
    {
        uint8_t DirectoryInodeData[256];
        if (!ReadInodeData(DirectoryInodeNumber, DirectoryInodeData))
        {
            return false;
        }

        for (uint32_t PointerIndex = 0; PointerIndex < 12; ++PointerIndex)
        {
            uint32_t BlockNumber = ReadLE32(&DirectoryInodeData[40 + (PointerIndex * 4)]);
            if (BlockNumber == 0)
            {
                continue;
            }

            uint64_t BlockOffset64 = static_cast<uint64_t>(BlockNumber) * BlockSizeBytes;
            if (BlockOffset64 > 0xFFFFFFFFu)
            {
                return false;
            }

            uint8_t* BlockData = new uint8_t[BlockSizeBytes];
            if (BlockData == nullptr)
            {
                return false;
            }

            if (!ReadBytesFromDisk(static_cast<uint32_t>(BlockOffset64), BlockData, BlockSizeBytes))
            {
                delete[] BlockData;
                return false;
            }

            uint32_t EntryOffset = 0;
            while (EntryOffset + 8 <= BlockSizeBytes)
            {
                uint16_t EntryLength = ReadLE16(&BlockData[EntryOffset + 4]);
                uint8_t  NameLength  = BlockData[EntryOffset + 6];

                if (EntryLength < 8 || (EntryOffset + EntryLength) > BlockSizeBytes)
                {
                    break;
                }

                if (NameLength == 2 && BlockData[EntryOffset + 8] == '.' && BlockData[EntryOffset + 9] == '.')
                {
                    WriteLE32(&BlockData[EntryOffset], NewParentInodeNumber);
                    bool WriteOk = WriteBytesToDisk(static_cast<uint32_t>(BlockOffset64), BlockData, BlockSizeBytes);
                    delete[] BlockData;
                    return WriteOk;
                }

                EntryOffset += EntryLength;
            }

            delete[] BlockData;
        }

        return false;
    };

    char     OldSegments[MAX_PATH_SEGMENTS][MAX_SEGMENT_CHARS + 1];
    uint32_t OldSegmentLengths[MAX_PATH_SEGMENTS];
    uint32_t OldSegmentCount = 0;
    if (!ParsePath(OldPath, OldSegments, OldSegmentLengths, &OldSegmentCount))
    {
        return false;
    }

    char     NewSegments[MAX_PATH_SEGMENTS][MAX_SEGMENT_CHARS + 1];
    uint32_t NewSegmentLengths[MAX_PATH_SEGMENTS];
    uint32_t NewSegmentCount = 0;
    if (!ParsePath(NewPath, NewSegments, NewSegmentLengths, &NewSegmentCount))
    {
        return false;
    }

    bool SamePath = (OldSegmentCount == NewSegmentCount);
    if (SamePath)
    {
        for (uint32_t SegmentIndex = 0; SegmentIndex < OldSegmentCount; ++SegmentIndex)
        {
            if (OldSegmentLengths[SegmentIndex] != NewSegmentLengths[SegmentIndex])
            {
                SamePath = false;
                break;
            }

            for (uint32_t NameIndex = 0; NameIndex < OldSegmentLengths[SegmentIndex]; ++NameIndex)
            {
                if (OldSegments[SegmentIndex][NameIndex] != NewSegments[SegmentIndex][NameIndex])
                {
                    SamePath = false;
                    break;
                }
            }

            if (!SamePath)
            {
                break;
            }
        }
    }

    if (SamePath)
    {
        return true;
    }

    uint32_t OldParentInode = 0;
    if (!ResolveParentInode(OldSegments, OldSegmentLengths, OldSegmentCount, &OldParentInode))
    {
        return false;
    }

    uint32_t NewParentInode = 0;
    if (!ResolveParentInode(NewSegments, NewSegmentLengths, NewSegmentCount, &NewParentInode))
    {
        return false;
    }

    const char* OldName      = OldSegments[OldSegmentCount - 1];
    uint32_t    OldNameBytes = OldSegmentLengths[OldSegmentCount - 1];
    const char* NewName      = NewSegments[NewSegmentCount - 1];
    uint32_t    NewNameBytes = NewSegmentLengths[NewSegmentCount - 1];

    uint32_t TargetInodeNumber = 0;
    bool     TargetIsDirectory = false;
    bool     TargetFound       = false;
    if (!FindEntryInDirectory(OldParentInode, OldName, OldNameBytes, &TargetInodeNumber, &TargetIsDirectory, &TargetFound))
    {
        return false;
    }

    if (!TargetFound || TargetInodeNumber == 0)
    {
        return false;
    }

    if ((RenamingDirectory && !TargetIsDirectory) || (RenamingRegularFile && TargetIsDirectory))
    {
        return false;
    }

    bool DestinationExists = false;
    if (!FindEntryInDirectory(NewParentInode, NewName, NewNameBytes, nullptr, nullptr, &DestinationExists))
    {
        return false;
    }

    if (DestinationExists)
    {
        return false;
    }

    if (RenamingDirectory)
    {
        if (NewParentInode == TargetInodeNumber)
        {
            return false;
        }

        uint32_t Cursor = NewParentInode;
        for (;;)
        {
            if (Cursor == TargetInodeNumber)
            {
                return false;
            }

            if (Cursor == EXT2_ROOT_INODE_NUMBER)
            {
                break;
            }

            uint32_t Parent = 0;
            if (!GetParentDirectoryInode(Cursor, &Parent))
            {
                return false;
            }

            if (Parent == 0 || Parent == Cursor)
            {
                return false;
            }

            Cursor = Parent;
        }
    }

    uint8_t EntryTypeByte = RenamingDirectory ? 2 : 1;
    if (!AddDirectoryEntry(NewParentInode, NewName, NewNameBytes, TargetInodeNumber, EntryTypeByte))
    {
        return false;
    }

    if (!RemoveDirectoryEntry(OldParentInode, OldName, OldNameBytes, TargetInodeNumber))
    {
        RemoveDirectoryEntry(NewParentInode, NewName, NewNameBytes, TargetInodeNumber);
        return false;
    }

    if (RenamingDirectory && OldParentInode != NewParentInode)
    {
        if (!UpdateDotDotEntry(TargetInodeNumber, NewParentInode))
        {
            return false;
        }

        uint8_t OldParentInodeData[256];
        uint8_t NewParentInodeData[256];
        if (!ReadInodeData(OldParentInode, OldParentInodeData) || !ReadInodeData(NewParentInode, NewParentInodeData))
        {
            return false;
        }

        uint16_t OldLinks = ReadLE16(&OldParentInodeData[26]);
        uint16_t NewLinks = ReadLE16(&NewParentInodeData[26]);
        if (OldLinks == 0)
        {
            return false;
        }

        WriteLE16(&OldParentInodeData[26], static_cast<uint16_t>(OldLinks - 1));
        WriteLE16(&NewParentInodeData[26], static_cast<uint16_t>(NewLinks + 1));

        if (!WriteInodeData(OldParentInode, OldParentInodeData))
        {
            return false;
        }

        if (!WriteInodeData(NewParentInode, NewParentInodeData))
        {
            return false;
        }
    }

    return true;
}

bool ExtendedFileSystemManager::EnumerateEntries(ExtendedFileSystemEntryCallback Callback, void* Context, TTY* Terminal) const
{
    (void) Terminal;

    if (!Initialized || Callback == nullptr)
    {
        return false;
    }

    ExtendedFileSystemEntry RootEntry = {};
    RootEntry.Name                    = "/";
    RootEntry.Data                    = nullptr;
    RootEntry.Size                    = 0;
    RootEntry.Type                    = ExtendedFileSystemEntryTypeDirectory;
    RootEntry.InodeNumber             = EXT2_ROOT_INODE_NUMBER;

    if (!Callback(RootEntry, Context))
    {
        return false;
    }

    uint64_t EnumeratedEntries = 0;
    bool     EnumerationResult = EnumerateDirectoryEntries(EXT2_ROOT_INODE_NUMBER, "/", Callback, Context, Terminal, &EnumeratedEntries);

    if (Terminal != nullptr)
    {
        Terminal->printf_("ext enum dbg: completed result=%u total_entries=%llu\n", EnumerationResult ? 1U : 0U, static_cast<unsigned long long>(EnumeratedEntries));
    }

    return EnumerationResult;
}

void ExtendedFileSystemManager::PrintFileSystem(TTY* Terminal) const
{
    if (Terminal == nullptr)
    {
        return;
    }

    if (!Initialized)
    {
        Terminal->printf_("ext2: filesystem is not initialized\n");
        return;
    }

    Terminal->printf_("ext2: partition_start_lba=%lu partition_sectors=%lu\n", static_cast<unsigned long>(PartitionStartLBA), static_cast<unsigned long>(PartitionSectorCount));
    Terminal->printf_("ext2: block_size=%u blocks=%u free_blocks=%u\n", BlockSizeBytes, BlocksCount, FreeBlocksCount);
    Terminal->printf_("ext2: inodes=%u free_inodes=%u inodes_per_group=%u blocks_per_group=%u\n", InodesCount, FreeInodesCount, InodesPerGroup, BlocksPerGroup);
    Terminal->printf_("ext2: first_data_block=%u volume_name='%.16s'\n", FirstDataBlock, VolumeName);
}

void ExtendedFileSystemManager::PrintFileTree(TTY* Terminal) const
{
    if (Terminal == nullptr)
    {
        return;
    }

    if (!Initialized)
    {
        Terminal->printf_("ext2 tree unavailable: filesystem is not initialized\n");
        return;
    }

    Terminal->printf_("EXT2 tree:\n");
    Terminal->printf_("/ [dir] size=0\n");
    PrintDirectoryTree(EXT2_ROOT_INODE_NUMBER, Terminal, 0, 3);
}