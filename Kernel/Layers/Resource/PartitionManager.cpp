/**
 * File: PartitionManager.cpp
 * Author: Marwan Mostafa
 * Description: Disk partition discovery implementation.
 */

#include "PartitionManager.hpp"

#include "DeviceManager.hpp"
#include "Drivers/IDEController.hpp"
#include "Layers/Logic/VirtualFileSystem.hpp"
#include "TTY.hpp"

#include <CommonUtils.hpp>

namespace
{
struct GptHeader
{
    char     Signature[8];
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t HeaderCrc32;
    uint32_t Reserved;
    uint64_t CurrentLBA;
    uint64_t BackupLBA;
    uint64_t FirstUsableLBA;
    uint64_t LastUsableLBA;
    uint8_t  DiskGuid[16];
    uint64_t PartitionEntryLBA;
    uint32_t NumberOfPartitionEntries;
    uint32_t SizeOfPartitionEntry;
    uint32_t PartitionEntryArrayCrc32;
} __attribute__((packed));

struct GptPartitionEntryPrefix
{
    uint8_t  TypeGuid[16];
    uint8_t  UniqueGuid[16];
    uint64_t FirstLBA;
    uint64_t LastLBA;
    uint64_t Attributes;
    uint16_t Name[36];
} __attribute__((packed));

static_assert(sizeof(GptHeader) <= 512, "GPT header must fit in one sector");
static_assert(sizeof(GptPartitionEntryPrefix) == 128, "GPT partition entry prefix size mismatch");

constexpr uint8_t GPT_LINUX_FILESYSTEM_TYPE_GUID[16] = {0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4};

constexpr uint32_t EXT2_SUPERBLOCK_OFFSET_BYTES = 1024;
constexpr uint32_t EXT2_SIGNATURE_OFFSET_BYTES  = 56;
constexpr uint16_t EXT2_SIGNATURE               = 0xEF53;

constexpr int64_t LINUX_ERR_EIO    = -5;
constexpr int64_t LINUX_ERR_EINVAL = -22;
constexpr int64_t LINUX_ERR_ESPIPE = -29;

constexpr int32_t LINUX_SEEK_SET = 0;
constexpr int32_t LINUX_SEEK_CUR = 1;
constexpr int32_t LINUX_SEEK_END = 2;

constexpr uint64_t PARTITION_SECTOR_SIZE       = 512;
constexpr uint32_t MAX_PARTITION_DEVICE_COUNT  = 64;
constexpr uint32_t PARTITION_PATH_BUFFER_BYTES = 16;

struct PartitionBlockDevice
{
    const DeviceManager* DeviceManagerInstance;
    uint64_t             StartLBA;
    uint64_t             SectorCount;
    uint32_t             PartitionIndex;
    char                 DevicePath[PARTITION_PATH_BUFFER_BYTES];
};

PartitionBlockDevice PartitionDevices[MAX_PARTITION_DEVICE_COUNT] = {};

bool SignatureMatchesGpt(const char Signature[8])
{
    return Signature[0] == 'E' && Signature[1] == 'F' && Signature[2] == 'I' && Signature[3] == ' ' && Signature[4] == 'P' && Signature[5] == 'A' && Signature[6] == 'R' && Signature[7] == 'T';
}

bool GuidMatchesLinuxFilesystemType(const uint8_t* Guid)
{
    for (uint32_t Index = 0; Index < 16; ++Index)
    {
        if (Guid[Index] != GPT_LINUX_FILESYSTEM_TYPE_GUID[Index])
        {
            return false;
        }
    }

    return true;
}

bool IsZeroGuid(const uint8_t* Guid)
{
    for (uint32_t Index = 0; Index < 16; ++Index)
    {
        if (Guid[Index] != 0)
        {
            return false;
        }
    }

    return true;
}

bool PartitionNameMatchesRootFs(const uint16_t Name[36])
{
    constexpr char Expected[] = "ROOTFS";

    for (uint32_t Index = 0; Index < 6; ++Index)
    {
        if (Name[Index] != static_cast<uint16_t>(Expected[Index]))
        {
            return false;
        }
    }

    return Name[6] == 0;
}

bool PartitionEntryNameMatchesRootFs(const GptPartitionEntryPrefix* Entry)
{
    if (Entry == nullptr)
    {
        return false;
    }

    uint16_t EntryName[36] = {};
    for (uint32_t Index = 0; Index < 36; ++Index)
    {
        EntryName[Index] = Entry->Name[Index];
    }

    return PartitionNameMatchesRootFs(EntryName);
}

uint16_t ReadLE16(const uint8_t* Data)
{
    return static_cast<uint16_t>(Data[0]) | static_cast<uint16_t>(static_cast<uint16_t>(Data[1]) << 8);
}

bool PartitionContainsExt2(const DeviceManager* DeviceManagerInstance, uint64_t PartitionStartLBA)
{
    if (DeviceManagerInstance == nullptr)
    {
        return false;
    }

    constexpr uint32_t SectorSize              = 512;
    constexpr uint32_t SuperblockLBAOffset     = EXT2_SUPERBLOCK_OFFSET_BYTES / SectorSize;
    constexpr uint32_t SignatureOffsetInSector = EXT2_SIGNATURE_OFFSET_BYTES;

    if (PartitionStartLBA > 0xFFFFFFFFu || (PartitionStartLBA + SuperblockLBAOffset) > 0xFFFFFFFFu)
    {
        return false;
    }

    uint8_t  SuperblockSector[SectorSize] = {};
    uint32_t SuperblockLBA                = static_cast<uint32_t>(PartitionStartLBA + SuperblockLBAOffset);
    if (!DeviceManagerInstance->ReadBlock(SuperblockLBA, SuperblockSector))
    {
        return false;
    }

    return ReadLE16(&SuperblockSector[SignatureOffsetInSector]) == EXT2_SIGNATURE;
}

uint64_t MinU64(uint64_t Left, uint64_t Right)
{
    return (Left < Right) ? Left : Right;
}

bool DevicePathsEqual(const char* Left, const char* Right)
{
    if (Left == nullptr || Right == nullptr)
    {
        return false;
    }

    for (uint32_t Index = 0; Index < PARTITION_PATH_BUFFER_BYTES; ++Index)
    {
        if (Left[Index] != Right[Index])
        {
            return false;
        }

        if (Left[Index] == '\0')
        {
            return true;
        }
    }

    return true;
}

bool BuildPartitionDevicePath(uint32_t PartitionIndex, char* Buffer, uint32_t BufferBytes)
{
    if (Buffer == nullptr || BufferBytes < PARTITION_PATH_BUFFER_BYTES)
    {
        return false;
    }

    Buffer[0] = '/';
    Buffer[1] = 'd';
    Buffer[2] = 'e';
    Buffer[3] = 'v';
    Buffer[4] = '/';
    Buffer[5] = 's';
    Buffer[6] = 'd';
    Buffer[7] = 'a';

    char     Digits[10]  = {};
    uint32_t DigitCount  = 0;
    uint32_t LocalIndex  = PartitionIndex;
    uint32_t WriteOffset = 8;

    if (LocalIndex == 0)
    {
        Digits[DigitCount++] = '0';
    }
    else
    {
        while (LocalIndex != 0 && DigitCount < sizeof(Digits))
        {
            Digits[DigitCount++] = static_cast<char>('0' + (LocalIndex % 10));
            LocalIndex /= 10;
        }
    }

    if ((WriteOffset + DigitCount + 1) > BufferBytes)
    {
        return false;
    }

    for (uint32_t DigitIndex = 0; DigitIndex < DigitCount; ++DigitIndex)
    {
        Buffer[WriteOffset + DigitIndex] = Digits[DigitCount - 1 - DigitIndex];
    }

    Buffer[WriteOffset + DigitCount] = '\0';
    return true;
}

int64_t PartitionReadFileOperation(File* OpenFile, void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr || Buffer == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    PartitionBlockDevice* Device = reinterpret_cast<PartitionBlockDevice*>(OpenFile->Node->NodeData);
    if (Device == nullptr || Device->DeviceManagerInstance == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t DeviceSizeBytes = Device->SectorCount * PARTITION_SECTOR_SIZE;
    if (OpenFile->CurrentOffset >= DeviceSizeBytes)
    {
        return 0;
    }

    uint64_t BytesRemaining = DeviceSizeBytes - OpenFile->CurrentOffset;
    uint64_t BytesToRead    = MinU64(Count, BytesRemaining);
    uint64_t TotalRead      = 0;

    while (TotalRead < BytesToRead)
    {
        uint64_t AbsoluteOffset      = OpenFile->CurrentOffset + TotalRead;
        uint64_t RelativeSectorIndex = AbsoluteOffset / PARTITION_SECTOR_SIZE;
        uint32_t SectorOffset        = static_cast<uint32_t>(AbsoluteOffset % PARTITION_SECTOR_SIZE);

        if ((Device->StartLBA + RelativeSectorIndex) > 0xFFFFFFFFu)
        {
            return (TotalRead == 0) ? LINUX_ERR_EIO : static_cast<int64_t>(TotalRead);
        }

        uint8_t  SectorBuffer[PARTITION_SECTOR_SIZE] = {};
        uint32_t AbsoluteLBA                         = static_cast<uint32_t>(Device->StartLBA + RelativeSectorIndex);
        if (!Device->DeviceManagerInstance->ReadBlock(AbsoluteLBA, SectorBuffer))
        {
            return (TotalRead == 0) ? LINUX_ERR_EIO : static_cast<int64_t>(TotalRead);
        }

        uint64_t BytesInSector = PARTITION_SECTOR_SIZE - SectorOffset;
        uint64_t Chunk         = MinU64(BytesInSector, BytesToRead - TotalRead);

        memcpy(reinterpret_cast<uint8_t*>(Buffer) + TotalRead, SectorBuffer + SectorOffset, static_cast<size_t>(Chunk));
        TotalRead += Chunk;
    }

    OpenFile->CurrentOffset += TotalRead;
    return static_cast<int64_t>(TotalRead);
}

int64_t PartitionWriteFileOperation(File* OpenFile, const void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr || Buffer == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    PartitionBlockDevice* Device = reinterpret_cast<PartitionBlockDevice*>(OpenFile->Node->NodeData);
    if (Device == nullptr || Device->DeviceManagerInstance == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t DeviceSizeBytes = Device->SectorCount * PARTITION_SECTOR_SIZE;
    if (OpenFile->CurrentOffset >= DeviceSizeBytes)
    {
        return 0;
    }

    uint64_t BytesRemaining = DeviceSizeBytes - OpenFile->CurrentOffset;
    uint64_t BytesToWrite   = MinU64(Count, BytesRemaining);
    uint64_t TotalWritten   = 0;

    while (TotalWritten < BytesToWrite)
    {
        uint64_t AbsoluteOffset      = OpenFile->CurrentOffset + TotalWritten;
        uint64_t RelativeSectorIndex = AbsoluteOffset / PARTITION_SECTOR_SIZE;
        uint32_t SectorOffset        = static_cast<uint32_t>(AbsoluteOffset % PARTITION_SECTOR_SIZE);

        if ((Device->StartLBA + RelativeSectorIndex) > 0xFFFFFFFFu)
        {
            return (TotalWritten == 0) ? LINUX_ERR_EIO : static_cast<int64_t>(TotalWritten);
        }

        uint8_t  SectorBuffer[PARTITION_SECTOR_SIZE] = {};
        uint32_t AbsoluteLBA                         = static_cast<uint32_t>(Device->StartLBA + RelativeSectorIndex);
        uint64_t BytesInSector                       = PARTITION_SECTOR_SIZE - SectorOffset;
        uint64_t Chunk                               = MinU64(BytesInSector, BytesToWrite - TotalWritten);

        if (SectorOffset != 0 || Chunk < PARTITION_SECTOR_SIZE)
        {
            if (!Device->DeviceManagerInstance->ReadBlock(AbsoluteLBA, SectorBuffer))
            {
                return (TotalWritten == 0) ? LINUX_ERR_EIO : static_cast<int64_t>(TotalWritten);
            }
        }

        memcpy(SectorBuffer + SectorOffset, reinterpret_cast<const uint8_t*>(Buffer) + TotalWritten, static_cast<size_t>(Chunk));

        if (!Device->DeviceManagerInstance->WriteBlock(AbsoluteLBA, SectorBuffer))
        {
            return (TotalWritten == 0) ? LINUX_ERR_EIO : static_cast<int64_t>(TotalWritten);
        }

        TotalWritten += Chunk;
    }

    OpenFile->CurrentOffset += TotalWritten;
    return static_cast<int64_t>(TotalWritten);
}

int64_t PartitionSeekFileOperation(File* OpenFile, int64_t Offset, int32_t Whence)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    PartitionBlockDevice* Device = reinterpret_cast<PartitionBlockDevice*>(OpenFile->Node->NodeData);
    if (Device == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t DeviceSizeBytes = Device->SectorCount * PARTITION_SECTOR_SIZE;
    int64_t  NewOffset       = 0;

    if (Whence == LINUX_SEEK_SET)
    {
        NewOffset = Offset;
    }
    else if (Whence == LINUX_SEEK_CUR)
    {
        NewOffset = static_cast<int64_t>(OpenFile->CurrentOffset) + Offset;
    }
    else if (Whence == LINUX_SEEK_END)
    {
        NewOffset = static_cast<int64_t>(DeviceSizeBytes) + Offset;
    }
    else
    {
        return LINUX_ERR_EINVAL;
    }

    if (NewOffset < 0 || static_cast<uint64_t>(NewOffset) > DeviceSizeBytes)
    {
        return LINUX_ERR_EINVAL;
    }

    OpenFile->CurrentOffset = static_cast<uint64_t>(NewOffset);
    return NewOffset;
}

int64_t PartitionMemoryMapFileOperation(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address)
{
    (void) OpenFile;
    (void) Length;
    (void) Offset;
    (void) AddressSpace;
    (void) Address;
    return LINUX_ERR_ESPIPE;
}

int64_t PartitionIoctlFileOperation(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic)
{
    (void) OpenFile;
    (void) Request;
    (void) Argument;
    (void) Logic;
    return LINUX_ERR_ESPIPE;
}

FileOperations PartitionBlockDeviceFileOperations = {
        &PartitionReadFileOperation,
        &PartitionWriteFileOperation,
        &PartitionSeekFileOperation,
        &PartitionMemoryMapFileOperation,
        &PartitionIoctlFileOperation,
};
} // namespace

PartitionManager::PartitionManager() : CachedPartitionCount(0)
{
    for (uint32_t Index = 0; Index < MAX_PARTITION_DEVICE_COUNT; ++Index)
    {
        CachedPartitions[Index] = {};
    }
}

bool PartitionManager::RefreshPartitionCache(const DeviceManager* DeviceManagerInstance)
{
    RootFileSystemPartitionInfo Scratch[MAX_PARTITION_DEVICE_COUNT] = {};
    uint32_t                    DiscoveredCount                      = 0;
    return EnumeratePartitions(DeviceManagerInstance, Scratch, MAX_PARTITION_DEVICE_COUNT, &DiscoveredCount);
}

bool PartitionManager::GetPartitionByDevicePath(const char* DevicePath, RootFileSystemPartitionInfo* PartitionInfo) const
{
    if (DevicePath == nullptr || PartitionInfo == nullptr)
    {
        return false;
    }

    for (uint32_t Index = 0; Index < CachedPartitionCount; ++Index)
    {
        if (DevicePathsEqual(CachedPartitions[Index].DevicePath, DevicePath))
        {
            *PartitionInfo = CachedPartitions[Index];
            return true;
        }
    }

    return false;
}

bool PartitionManager::GetCachedPartitionByIndex(uint32_t Index, RootFileSystemPartitionInfo* PartitionInfo) const
{
    if (PartitionInfo == nullptr || Index >= CachedPartitionCount)
    {
        return false;
    }

    *PartitionInfo = CachedPartitions[Index];
    return true;
}

uint32_t PartitionManager::GetCachedPartitionCount() const
{
    return CachedPartitionCount;
}

bool PartitionManager::EnumeratePartitions(const DeviceManager* DeviceManagerInstance, RootFileSystemPartitionInfo* PartitionInfos, uint32_t MaxPartitionCount,
                                          uint32_t* PartitionCount)
{
    TTY* Terminal = (DeviceManagerInstance == nullptr) ? nullptr : DeviceManagerInstance->GetLogTerminal();

    if (DeviceManagerInstance == nullptr || PartitionCount == nullptr || (PartitionInfos == nullptr && MaxPartitionCount != 0))
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: enumerate invalid arguments\n");
        }
        return false;
    }

    *PartitionCount = 0;
    CachedPartitionCount = 0;

    IDEController* Controller = DeviceManagerInstance->GetPrimaryIDEController();
    if (Controller == nullptr || !Controller->IsInitialized())
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: enumerate IDE controller unavailable\n");
        }
        return false;
    }

    uint8_t SectorBuffer[512] = {};
    if (!DeviceManagerInstance->ReadBlock(1, SectorBuffer))
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: enumerate failed reading GPT header LBA 1\n");
        }
        return false;
    }

    const GptHeader* Header = reinterpret_cast<const GptHeader*>(SectorBuffer);
    if (!SignatureMatchesGpt(Header->Signature) || Header->SizeOfPartitionEntry < sizeof(GptPartitionEntryPrefix) || Header->NumberOfPartitionEntries == 0)
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: enumerate invalid GPT header sig=%c%c%c%c%c%c%c%c entry_size=%u entries=%u\n", Header->Signature[0], Header->Signature[1],
                              Header->Signature[2], Header->Signature[3], Header->Signature[4], Header->Signature[5], Header->Signature[6], Header->Signature[7],
                              Header->SizeOfPartitionEntry, Header->NumberOfPartitionEntries);
        }
        return false;
    }

    uint32_t EntryCount = Header->NumberOfPartitionEntries;
    if (EntryCount > 256)
    {
        EntryCount = 256;
    }

    for (uint32_t EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
    {
        uint64_t EntryByteOffset = static_cast<uint64_t>(EntryIndex) * Header->SizeOfPartitionEntry;
        uint64_t EntryLBA64      = Header->PartitionEntryLBA + (EntryByteOffset / 512u);
        uint32_t EntryOffset     = static_cast<uint32_t>(EntryByteOffset % 512u);

        if (EntryLBA64 > 0xFFFFFFFFu || (EntryOffset + sizeof(GptPartitionEntryPrefix)) > 512u)
        {
            continue;
        }

        if (!DeviceManagerInstance->ReadBlock(static_cast<uint32_t>(EntryLBA64), SectorBuffer))
        {
            if (Terminal != nullptr)
            {
                Terminal->printf_("partition scan: enumerate failed reading partition entry block lba=%lu\n", static_cast<unsigned long>(EntryLBA64));
            }
            return false;
        }

        const GptPartitionEntryPrefix* Entry = reinterpret_cast<const GptPartitionEntryPrefix*>(SectorBuffer + EntryOffset);
        if (IsZeroGuid(Entry->TypeGuid) || Entry->LastLBA < Entry->FirstLBA)
        {
            continue;
        }

        if (*PartitionCount < MaxPartitionCount)
        {
            RootFileSystemPartitionInfo& Info = PartitionInfos[*PartitionCount];
            Info.StartLBA                     = Entry->FirstLBA;
            Info.SectorCount                  = (Entry->LastLBA - Entry->FirstLBA) + 1;
            Info.PartitionIndex               = EntryIndex + 1;
            if (!BuildPartitionDevicePath(Info.PartitionIndex, Info.DevicePath, sizeof(Info.DevicePath)))
            {
                Info.DevicePath[0] = '\0';
            }
        }

        if (CachedPartitionCount < MAX_PARTITION_DEVICE_COUNT)
        {
            RootFileSystemPartitionInfo& CachedInfo = CachedPartitions[CachedPartitionCount];
            CachedInfo.StartLBA                     = Entry->FirstLBA;
            CachedInfo.SectorCount                  = (Entry->LastLBA - Entry->FirstLBA) + 1;
            CachedInfo.PartitionIndex               = EntryIndex + 1;
            if (!BuildPartitionDevicePath(CachedInfo.PartitionIndex, CachedInfo.DevicePath, sizeof(CachedInfo.DevicePath)))
            {
                CachedInfo.DevicePath[0] = '\0';
            }

            ++CachedPartitionCount;
        }

        ++(*PartitionCount);
    }

    return true;
}

bool PartitionManager::RegisterPartitionDevices(const DeviceManager* DeviceManagerInstance, VirtualFileSystem* VFS)
{
    TTY* Terminal = (DeviceManagerInstance == nullptr) ? nullptr : DeviceManagerInstance->GetLogTerminal();

    if (DeviceManagerInstance == nullptr || VFS == nullptr)
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition device registration skipped: prerequisites missing\n");
        }
        return false;
    }

    if (!RefreshPartitionCache(DeviceManagerInstance))
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition device registration failed: partition enumeration failed\n");
        }
        return false;
    }

    uint32_t DetectedPartitionCount = CachedPartitionCount;

    if (DetectedPartitionCount == 0)
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition device registration: no partitions found\n");
        }
        return true;
    }

    if (DetectedPartitionCount > MAX_PARTITION_DEVICE_COUNT && Terminal != nullptr)
    {
        Terminal->printf_("partition device registration: truncating partitions from %u to %u\n", DetectedPartitionCount, MAX_PARTITION_DEVICE_COUNT);
    }

    uint32_t RegistrationCount = (DetectedPartitionCount > MAX_PARTITION_DEVICE_COUNT) ? MAX_PARTITION_DEVICE_COUNT : DetectedPartitionCount;
    bool     RegisteredAny     = false;

    for (uint32_t Index = 0; Index < RegistrationCount; ++Index)
    {
        PartitionDevices[Index].DeviceManagerInstance = DeviceManagerInstance;
        PartitionDevices[Index].StartLBA              = CachedPartitions[Index].StartLBA;
        PartitionDevices[Index].SectorCount           = CachedPartitions[Index].SectorCount;
        PartitionDevices[Index].PartitionIndex        = CachedPartitions[Index].PartitionIndex;
        PartitionDevices[Index].DevicePath[0]         = '\0';

        for (uint32_t PathCharIndex = 0; PathCharIndex < PARTITION_PATH_BUFFER_BYTES; ++PathCharIndex)
        {
            PartitionDevices[Index].DevicePath[PathCharIndex] = CachedPartitions[Index].DevicePath[PathCharIndex];
            if (CachedPartitions[Index].DevicePath[PathCharIndex] == '\0')
            {
                break;
            }
        }

        char DevicePath[PARTITION_PATH_BUFFER_BYTES] = {};
        if (PartitionDevices[Index].DevicePath[0] != '\0')
        {
            for (uint32_t PathCharIndex = 0; PathCharIndex < PARTITION_PATH_BUFFER_BYTES; ++PathCharIndex)
            {
                DevicePath[PathCharIndex] = PartitionDevices[Index].DevicePath[PathCharIndex];
                if (PartitionDevices[Index].DevicePath[PathCharIndex] == '\0')
                {
                    break;
                }
            }
        }
        else if (!BuildPartitionDevicePath(PartitionDevices[Index].PartitionIndex, DevicePath, sizeof(DevicePath)))
        {
            if (Terminal != nullptr)
            {
                Terminal->printf_("partition device registration: path build failed idx=%u\n", PartitionDevices[Index].PartitionIndex);
            }
            continue;
        }

        bool Registered = VFS->RegisterDevice(DevicePath, &PartitionDevices[Index], &PartitionBlockDeviceFileOperations);
        if (Terminal != nullptr)
        {
            if (Registered)
            {
                Terminal->printf_("partition device registration: %s start=%lu sectors=%lu\n", DevicePath, static_cast<unsigned long>(PartitionDevices[Index].StartLBA),
                                  static_cast<unsigned long>(PartitionDevices[Index].SectorCount));
            }
            else
            {
                Terminal->printf_("partition device registration: failed %s\n", DevicePath);
            }
        }

        if (Registered)
        {
            RegisteredAny = true;
        }
    }

    return RegisteredAny;
}

bool PartitionManager::LocateRootFileSystemPartition(const DeviceManager* DeviceManagerInstance, RootFileSystemPartitionInfo* PartitionInfo)
{
    TTY* Terminal = (DeviceManagerInstance == nullptr) ? nullptr : DeviceManagerInstance->GetLogTerminal();

    if (PartitionInfo == nullptr || DeviceManagerInstance == nullptr)
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: invalid arguments\n");
        }
        return false;
    }

    *PartitionInfo = {};

    if (Terminal != nullptr)
    {
        Terminal->printf_("partition scan: starting GPT discovery\n");
    }

    IDEController* Controller = DeviceManagerInstance->GetPrimaryIDEController();
    if (Controller == nullptr || !Controller->IsInitialized())
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: IDE controller unavailable\n");
        }
        return false;
    }

    uint8_t SectorBuffer[512] = {};
    if (!DeviceManagerInstance->ReadBlock(1, SectorBuffer))
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: failed reading GPT header LBA 1\n");
        }
        return false;
    }

    const GptHeader* Header = reinterpret_cast<const GptHeader*>(SectorBuffer);
    if (!SignatureMatchesGpt(Header->Signature) || Header->SizeOfPartitionEntry < sizeof(GptPartitionEntryPrefix) || Header->NumberOfPartitionEntries == 0)
    {
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: invalid GPT header sig=%c%c%c%c%c%c%c%c entry_size=%u entries=%u\n", Header->Signature[0], Header->Signature[1], Header->Signature[2],
                              Header->Signature[3], Header->Signature[4], Header->Signature[5], Header->Signature[6], Header->Signature[7], Header->SizeOfPartitionEntry,
                              Header->NumberOfPartitionEntries);
        }
        return false;
    }

    uint32_t EntryCount = Header->NumberOfPartitionEntries;
    if (EntryCount > 256)
    {
        EntryCount = 256;
    }

    if (Terminal != nullptr)
    {
        Terminal->printf_("partition scan: entries=%u entry_lba=%lu entry_size=%u\n", EntryCount, static_cast<unsigned long>(Header->PartitionEntryLBA), Header->SizeOfPartitionEntry);
    }

    RootFileSystemPartitionInfo LinuxTypeFallback       = {};
    bool                        HasLinuxTypeFallback    = false;
    RootFileSystemPartitionInfo Ext2Fallback            = {};
    bool                        HasExt2Fallback         = false;
    uint64_t                    HighestPartitionLastLBA = 0;

    for (uint32_t EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
    {
        uint64_t EntryByteOffset = static_cast<uint64_t>(EntryIndex) * Header->SizeOfPartitionEntry;
        uint64_t EntryLBA64      = Header->PartitionEntryLBA + (EntryByteOffset / 512u);
        uint32_t EntryOffset     = static_cast<uint32_t>(EntryByteOffset % 512u);

        if (EntryLBA64 > 0xFFFFFFFFu || (EntryOffset + sizeof(GptPartitionEntryPrefix)) > 512u)
        {
            if (Terminal != nullptr)
            {
                Terminal->printf_("partition scan: skip idx=%u (out of range)\n", EntryIndex + 1);
            }
            continue;
        }

        if (!DeviceManagerInstance->ReadBlock(static_cast<uint32_t>(EntryLBA64), SectorBuffer))
        {
            if (Terminal != nullptr)
            {
                Terminal->printf_("partition scan: failed reading partition entry block lba=%lu\n", static_cast<unsigned long>(EntryLBA64));
            }
            return false;
        }

        const GptPartitionEntryPrefix* Entry = reinterpret_cast<const GptPartitionEntryPrefix*>(SectorBuffer + EntryOffset);

        if (IsZeroGuid(Entry->TypeGuid))
        {
            continue;
        }

        if (Entry->LastLBA < Entry->FirstLBA)
        {
            if (Terminal != nullptr)
            {
                Terminal->printf_("partition scan: skip idx=%u invalid range first=%lu last=%lu\n", EntryIndex + 1, static_cast<unsigned long>(Entry->FirstLBA),
                                  static_cast<unsigned long>(Entry->LastLBA));
            }
            continue;
        }

        RootFileSystemPartitionInfo Candidate = {};
        Candidate.StartLBA                    = Entry->FirstLBA;
        Candidate.SectorCount                 = (Entry->LastLBA - Entry->FirstLBA) + 1;
        Candidate.PartitionIndex              = EntryIndex + 1;
        if (!BuildPartitionDevicePath(Candidate.PartitionIndex, Candidate.DevicePath, sizeof(Candidate.DevicePath)))
        {
            Candidate.DevicePath[0] = '\0';
        }

        if (Entry->LastLBA > HighestPartitionLastLBA)
        {
            HighestPartitionLastLBA = Entry->LastLBA;
        }

        bool IsLinuxType = GuidMatchesLinuxFilesystemType(Entry->TypeGuid);
        bool IsRootName  = PartitionEntryNameMatchesRootFs(Entry);
        bool IsExt2      = PartitionContainsExt2(DeviceManagerInstance, Candidate.StartLBA);

        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: idx=%u start=%lu sectors=%lu linux_type=%u root_name=%u ext2=%u\n", Candidate.PartitionIndex, static_cast<unsigned long>(Candidate.StartLBA),
                              static_cast<unsigned long>(Candidate.SectorCount), IsLinuxType ? 1u : 0u, IsRootName ? 1u : 0u, IsExt2 ? 1u : 0u);
        }

        if (IsRootName && IsExt2)
        {
            *PartitionInfo = Candidate;
            if (Terminal != nullptr)
            {
                Terminal->printf_("partition scan: selected ROOTFS ext2 partition idx=%u dev=%s\n", Candidate.PartitionIndex,
                                  (Candidate.DevicePath[0] != '\0') ? Candidate.DevicePath : "<none>");
            }
            return true;
        }

        if (IsRootName && Terminal != nullptr && !IsExt2)
        {
            Terminal->printf_("partition scan: ROOTFS name matched but ext2 signature missing on idx=%u\n", Candidate.PartitionIndex);
        }

        if (!HasExt2Fallback && IsExt2)
        {
            Ext2Fallback    = Candidate;
            HasExt2Fallback = true;
        }

        if (!HasLinuxTypeFallback && IsLinuxType)
        {
            LinuxTypeFallback    = Candidate;
            HasLinuxTypeFallback = true;
        }
    }

    if (HasExt2Fallback)
    {
        *PartitionInfo = Ext2Fallback;
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: selected ext2 fallback idx=%u dev=%s\n", Ext2Fallback.PartitionIndex,
                              (Ext2Fallback.DevicePath[0] != '\0') ? Ext2Fallback.DevicePath : "<none>");
        }
        return true;
    }

    if (HasLinuxTypeFallback)
    {
        *PartitionInfo = LinuxTypeFallback;
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: selected linux fallback idx=%u dev=%s\n", LinuxTypeFallback.PartitionIndex,
                              (LinuxTypeFallback.DevicePath[0] != '\0') ? LinuxTypeFallback.DevicePath : "<none>");
        }
        return true;
    }

    uint64_t ProbeStartLBA = 2048;
    if (HighestPartitionLastLBA >= ProbeStartLBA)
    {
        ProbeStartLBA      = HighestPartitionLastLBA + 1;
        uint64_t Alignment = 2048;
        ProbeStartLBA      = ((ProbeStartLBA + Alignment - 1) / Alignment) * Alignment;
    }

    constexpr uint64_t ProbeLimitLBA = 0x200000;
    for (uint64_t ProbeLBA = ProbeStartLBA; ProbeLBA <= ProbeLimitLBA; ProbeLBA += 2048)
    {
        if (PartitionContainsExt2(DeviceManagerInstance, ProbeLBA))
        {
            PartitionInfo->StartLBA       = ProbeLBA;
            PartitionInfo->SectorCount    = (0xFFFFFFFFu - static_cast<uint32_t>(ProbeLBA)) + 1;
            PartitionInfo->PartitionIndex = 0;
            PartitionInfo->DevicePath[0]  = '\0';

            if (Terminal != nullptr)
            {
                Terminal->printf_("partition scan: selected ext2 probe fallback start=%lu\n", static_cast<unsigned long>(ProbeLBA));
            }

            return true;
        }
    }

    if (Terminal != nullptr)
    {
        Terminal->printf_("partition scan: ext2 probe fallback exhausted start=%lu limit=%lu\n", static_cast<unsigned long>(ProbeStartLBA), static_cast<unsigned long>(ProbeLimitLBA));
    }

    if (Terminal != nullptr)
    {
        Terminal->printf_("partition scan: no matching Linux filesystem partition found\n");
    }

    return false;
}
