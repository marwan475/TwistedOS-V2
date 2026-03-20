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
constexpr uint32_t CONTROLLER_SECTOR_SIZE_BYTES = 512;
constexpr uint32_t EXT2_GROUP_DESCRIPTOR_SIZE   = 32;
constexpr uint32_t EXT2_ROOT_INODE_NUMBER       = 2;
constexpr uint16_t EXT2_INODE_MODE_DIRECTORY    = 0x4000;

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

    return ReadBytesFromDisk(static_cast<uint32_t>(InodeOffset), InodeData, InodeSizeBytes);
}

bool ExtendedFileSystemManager::EnumerateDirectoryEntries(uint32_t DirectoryInodeNumber, const char* DirectoryPath, ExtendedFileSystemEntryCallback Callback, void* Context) const
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
                    uint64_t EntrySize      = 0;
                    bool     IsDirectory    = false;
                    uint8_t  ChildInodeData[256] = {};

                    if (EntryInode <= InodesCount && InodeSizeBytes <= sizeof(ChildInodeData) && ReadInode(EntryInode, ChildInodeData, sizeof(ChildInodeData)))
                    {
                        EntrySize   = ReadLE32(&ChildInodeData[4]);
                        uint16_t ChildMode = ReadLE16(&ChildInodeData[0]);
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
                    Entry.Data                    = nullptr;
                    Entry.Size                    = EntrySize;
                    Entry.Type                    = DecodedType;
                    Entry.InodeNumber             = EntryInode;

                    bool ContinueEnumeration = Callback(Entry, Context);

                    bool RecurseIntoDirectory = (DecodedType == ExtendedFileSystemEntryTypeDirectory || IsDirectory);
                    if (ContinueEnumeration && RecurseIntoDirectory)
                    {
                        ContinueEnumeration = EnumerateDirectoryEntries(EntryInode, FullPath, Callback, Context);
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

    return EnumerateDirectoryEntries(EXT2_ROOT_INODE_NUMBER, "/", Callback, Context);
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