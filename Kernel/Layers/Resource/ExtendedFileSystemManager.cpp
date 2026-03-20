/**
 * File: ExtendedFileSystemManager.cpp
 * Author: Marwan Mostafa
 * Description: EXT2 filesystem manager implementation.
 */

#include "ExtendedFileSystemManager.hpp"

#include "Drivers/IDEController.hpp"
#include "TTY.hpp"

#include <Layers/Dispatcher.hpp>
#include <CommonUtils.hpp>

namespace
{
constexpr uint32_t CONTROLLER_SECTOR_SIZE_BYTES = 512;
constexpr uint32_t EXT2_GROUP_DESCRIPTOR_SIZE   = 32;
constexpr uint32_t EXT2_ROOT_INODE_NUMBER       = 2;
constexpr uint16_t EXT2_INODE_MODE_DIRECTORY    = 0x4000;
constexpr uint16_t EXT2_INODE_MODE_SYMLINK      = 0xA000;

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

bool StringEquals(const char* Left, const char* Right)
{
    if (Left == nullptr || Right == nullptr)
    {
        return false;
    }

    uint64_t LeftLength  = strlen(Left);
    uint64_t RightLength = strlen(Right);
    if (LeftLength != RightLength)
    {
        return false;
    }

    for (uint64_t Index = 0; Index < LeftLength; ++Index)
    {
        if (Left[Index] != Right[Index])
        {
            return false;
        }
    }

    return true;
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

#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
        {
            ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 inode load: ino=%u group=%u index=%u\n", InodeNumber, GroupIndex, InodeIndex);
        }
    }
#endif

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
#ifdef DEBUG_BUILD
        {
            Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
            if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
            {
                ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 inode load fail: ino=%u inode_table_block=0\n", InodeNumber);
            }
        }
#endif
        return false;
    }

    uint64_t InodeOffset = (static_cast<uint64_t>(InodeTableBlock) * BlockSizeBytes) + (static_cast<uint64_t>(InodeIndex) * InodeSizeBytes);
    if (InodeOffset > 0xFFFFFFFFu)
    {
#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
        {
        ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 inode load fail: ino=%u inode_offset_overflow=%llu\n", InodeNumber,
                                    static_cast<unsigned long long>(InodeOffset));
        }
    }
#endif
        return false;
    }

    bool ReadOk = ReadBytesFromDisk(static_cast<uint32_t>(InodeOffset), InodeData, InodeSizeBytes);
#ifdef DEBUG_BUILD
    if (!ReadOk)
    {
        {
            Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
            if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
            {
                ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 inode load fail: ino=%u inode_disk_read_failed\n", InodeNumber);
            }
        }
    }
    else
    {
        uint16_t Mode = ReadLE16(&InodeData[0]);
        uint32_t Size = ReadLE32(&InodeData[4]);
        {
            Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
            if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
            {
                ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 inode loaded: ino=%u mode=0x%x size=%u\n", InodeNumber, Mode, Size);
            }
        }
    }
#endif
    return ReadOk;
}

bool ExtendedFileSystemManager::ReadInodePayload(uint32_t InodeNumber, const uint8_t* InodeData, uint16_t InodeMode, uint64_t PayloadSize, void* DestinationBuffer) const
{
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

#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
        {
            ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 inode payload: ino=%u mode=0x%x size=%llu\n", InodeNumber, InodeMode,
                                                                    static_cast<unsigned long long>(PayloadSize));
        }
    }
#endif

    if ((InodeMode & 0xF000) == EXT2_INODE_MODE_SYMLINK && PayloadSize <= 60)
    {
        memcpy(Destination, &InodeData[40], static_cast<size_t>(PayloadSize));
#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
        {
        ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 inode payload inline-symlink: ino=%u size=%llu\n", InodeNumber,
                                    static_cast<unsigned long long>(PayloadSize));
        }
    }
#endif
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

#ifdef DEBUG_BUILD
        {
            Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
            if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
            {
                ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 inode payload block: ino=%u lblock=%llu pblock=%u bytes=%llu\n", InodeNumber,
                                                                        static_cast<unsigned long long>(LogicalBlock), DataBlock,
                                                                        static_cast<unsigned long long>(BytesThisBlock));
            }
        }
#endif

        if (DataBlock != 0)
        {
            uint64_t BlockByteOffset = static_cast<uint64_t>(DataBlock) * BlockSizeBytes;
            if (BlockByteOffset > 0xFFFFFFFFu)
            {
#ifdef DEBUG_BUILD
                {
                    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
                    if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
                    {
                        ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 inode payload fail: ino=%u lblock=%llu pblock_offset_overflow=%llu\n", InodeNumber,
                                                                                static_cast<unsigned long long>(LogicalBlock),
                                                                                static_cast<unsigned long long>(BlockByteOffset));
                    }
                }
#endif
                return false;
            }

            if (!ReadBytesFromDisk(static_cast<uint32_t>(BlockByteOffset), Destination + WriteOffset, static_cast<uint32_t>(BytesThisBlock)))
            {
#ifdef DEBUG_BUILD
                {
                    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
                    if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
                    {
                        ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 inode payload fail: ino=%u lblock=%llu disk_read_failed\n", InodeNumber,
                                                                                static_cast<unsigned long long>(LogicalBlock));
                    }
                }
#endif
                return false;
            }
        }

        RemainingBytes -= BytesThisBlock;
        WriteOffset += BytesThisBlock;
        ++LogicalBlock;
    }

#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
        {
            ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 inode payload loaded: ino=%u size=%llu\n", InodeNumber,
                                                                    static_cast<unsigned long long>(PayloadSize));
        }
    }
#endif
    return true;
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

#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
        {
            ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 enumerate dir: ino=%u path='%s'\n", DirectoryInodeNumber, DirectoryPath);
        }
    }
#endif

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
                        EntrySize          = ReadLE32(&ChildInodeData[4]);
                        ChildMode          = ReadLE16(&ChildInodeData[0]);
                        IsDirectory        = ((ChildMode & EXT2_INODE_MODE_DIRECTORY) != 0);
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

                    bool ShouldLoadData = (DecodedType == ExtendedFileSystemEntryTypeSymbolicLink);

                    if (!ShouldLoadData && DecodedType == ExtendedFileSystemEntryTypeRegularFile)
                    {
                        ShouldLoadData = StringEquals(FullPath, "/bin/busybox");
                    }

                    if (ShouldLoadData && EntrySize > 0)
                    {
#ifdef DEBUG_BUILD
                        {
                            Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
                            if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
                            {
                                ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 preload inode: ino=%u path='%s' type=%s size=%llu\n", EntryInode, FullPath,
                                                                                        (DecodedType == ExtendedFileSystemEntryTypeSymbolicLink) ? "symlink" : "file",
                                                                                        static_cast<unsigned long long>(EntrySize));
                            }
                        }
#endif

                        uint8_t* LoadedData    = nullptr;
                        bool     UsedKernelHeap = false;
                        bool     UsedPMM        = false;
                        uint64_t PMMPages       = 0;

                        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
                        if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr)
                        {
                            ResourceLayer* Resource = ActiveDispatcher->GetResourceLayer();

                            if (EntrySize >= (128 * 1024) && Resource->GetPMM() != nullptr)
                            {
                                PMMPages  = (EntrySize + 4095) / 4096;
                                LoadedData = reinterpret_cast<uint8_t*>(Resource->GetPMM()->AllocatePagesFromDescriptor(PMMPages));
                                UsedPMM    = (LoadedData != nullptr);
                            }

                            if (LoadedData == nullptr)
                            {
                                LoadedData     = reinterpret_cast<uint8_t*>(Resource->kmalloc(static_cast<size_t>(EntrySize)));
                                UsedKernelHeap = (LoadedData != nullptr);
                            }
                        }

                        if (LoadedData == nullptr)
                        {
                            LoadedData = new uint8_t[EntrySize];
                        }

                        if (LoadedData == nullptr)
                        {
                            delete[] FullPath;
                            delete[] BlockData;
                            return false;
                        }

#ifdef DEBUG_BUILD
                        {
                            Dispatcher* AllocationDispatcher = Dispatcher::GetActive();
                            if (AllocationDispatcher != nullptr && AllocationDispatcher->GetResourceLayer() != nullptr
                                && AllocationDispatcher->GetResourceLayer()->GetTTY() != nullptr)
                            {
                                AllocationDispatcher->GetResourceLayer()->GetTTY()->printf_(
                                        "ext2 preload alloc ok: ino=%u path='%s' size=%llu src=%s\n", EntryInode, FullPath, static_cast<unsigned long long>(EntrySize),
                                        UsedPMM ? "pmm" : (UsedKernelHeap ? "kmalloc" : "new"));
                            }
                        }
#endif

                        if (!ReadInodePayload(EntryInode, ChildInodeData, ChildMode, EntrySize, LoadedData))
                        {
                            if (UsedPMM)
                            {
                                Dispatcher* CleanupDispatcher = Dispatcher::GetActive();
                                if (CleanupDispatcher != nullptr && CleanupDispatcher->GetResourceLayer() != nullptr && CleanupDispatcher->GetResourceLayer()->GetPMM() != nullptr)
                                {
                                    CleanupDispatcher->GetResourceLayer()->GetPMM()->FreePagesFromDescriptor(LoadedData, PMMPages);
                                }
                            }
                            else if (UsedKernelHeap)
                            {
                                Dispatcher* CleanupDispatcher = Dispatcher::GetActive();
                                if (CleanupDispatcher != nullptr && CleanupDispatcher->GetResourceLayer() != nullptr)
                                {
                                    CleanupDispatcher->GetResourceLayer()->kfree(LoadedData);
                                }
                            }
                            else
                            {
                                delete[] LoadedData;
                            }

                            delete[] FullPath;
                            delete[] BlockData;
                            return false;
                        }

                        EntryData = LoadedData;
                    }

                    Entry.Data                    = EntryData;
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
    if (!Initialized || Callback == nullptr)
    {
        return false;
    }

#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
        {
            ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 enumerate start: root_inode=%u\n", EXT2_ROOT_INODE_NUMBER);
        }
    }
#endif

    ExtendedFileSystemEntry RootEntry = {};
    RootEntry.Name                    = "/";
    RootEntry.Data                    = nullptr;
    RootEntry.Size                    = 0;
    RootEntry.Type                    = ExtendedFileSystemEntryTypeDirectory;
    RootEntry.InodeNumber             = EXT2_ROOT_INODE_NUMBER;

    if (!Callback(RootEntry, Context))
    {
#ifdef DEBUG_BUILD
        {
            Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
            if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
            {
                ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 enumerate stop: root callback rejected\n");
            }
        }
#endif
        return false;
    }

    bool Result = EnumerateDirectoryEntries(EXT2_ROOT_INODE_NUMBER, "/", Callback, Context);

#ifdef DEBUG_BUILD
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr && ActiveDispatcher->GetResourceLayer()->GetTTY() != nullptr)
        {
            ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("ext2 enumerate done: result=%d\n", Result ? 1 : 0);
        }
    }
#endif

    return Result;
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