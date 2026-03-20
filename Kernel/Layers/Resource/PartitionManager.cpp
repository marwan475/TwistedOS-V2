/**
 * File: PartitionManager.cpp
 * Author: Marwan Mostafa
 * Description: Disk partition discovery implementation.
 */

#include "PartitionManager.hpp"

#include "DeviceManager.hpp"
#include "Drivers/IDEController.hpp"
#include "TTY.hpp"

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

constexpr uint8_t GPT_LINUX_FILESYSTEM_TYPE_GUID[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

constexpr uint32_t EXT2_SUPERBLOCK_OFFSET_BYTES = 1024;
constexpr uint32_t EXT2_SIGNATURE_OFFSET_BYTES  = 56;
constexpr uint16_t EXT2_SIGNATURE               = 0xEF53;

bool SignatureMatchesGpt(const char Signature[8])
{
    return Signature[0] == 'E' && Signature[1] == 'F' && Signature[2] == 'I' && Signature[3] == ' ' && Signature[4] == 'P' && Signature[5] == 'A' && Signature[6] == 'R'
        && Signature[7] == 'T';
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

    constexpr uint32_t SectorSize = 512;
    constexpr uint32_t SuperblockLBAOffset = EXT2_SUPERBLOCK_OFFSET_BYTES / SectorSize;
    constexpr uint32_t SignatureOffsetInSector = EXT2_SIGNATURE_OFFSET_BYTES;

    if (PartitionStartLBA > 0xFFFFFFFFu || (PartitionStartLBA + SuperblockLBAOffset) > 0xFFFFFFFFu)
    {
        return false;
    }

    uint8_t SuperblockSector[SectorSize] = {};
    uint32_t SuperblockLBA = static_cast<uint32_t>(PartitionStartLBA + SuperblockLBAOffset);
    if (!DeviceManagerInstance->ReadBlock(SuperblockLBA, SuperblockSector))
    {
        return false;
    }

    return ReadLE16(&SuperblockSector[SignatureOffsetInSector]) == EXT2_SIGNATURE;
}
} // namespace

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

    RootFileSystemPartitionInfo LinuxTypeFallback = {};
    bool HasLinuxTypeFallback = false;
    RootFileSystemPartitionInfo Ext2Fallback = {};
    bool HasExt2Fallback = false;
    uint64_t HighestPartitionLastLBA = 0;

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
        Candidate.StartLBA       = Entry->FirstLBA;
        Candidate.SectorCount    = (Entry->LastLBA - Entry->FirstLBA) + 1;
        Candidate.PartitionIndex = EntryIndex + 1;

        if (Entry->LastLBA > HighestPartitionLastLBA)
        {
            HighestPartitionLastLBA = Entry->LastLBA;
        }

        bool IsLinuxType = GuidMatchesLinuxFilesystemType(Entry->TypeGuid);
        bool IsRootName  = PartitionEntryNameMatchesRootFs(Entry);
        bool IsExt2      = PartitionContainsExt2(DeviceManagerInstance, Candidate.StartLBA);

        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: idx=%u start=%lu sectors=%lu linux_type=%u root_name=%u ext2=%u\n", Candidate.PartitionIndex,
                              static_cast<unsigned long>(Candidate.StartLBA), static_cast<unsigned long>(Candidate.SectorCount), IsLinuxType ? 1u : 0u, IsRootName ? 1u : 0u,
                              IsExt2 ? 1u : 0u);
        }

        if (IsRootName && IsExt2)
        {
            *PartitionInfo = Candidate;
            if (Terminal != nullptr)
            {
                Terminal->printf_("partition scan: selected ROOTFS ext2 partition idx=%u\n", Candidate.PartitionIndex);
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
            Terminal->printf_("partition scan: selected ext2 fallback idx=%u\n", Ext2Fallback.PartitionIndex);
        }
        return true;
    }

    if (HasLinuxTypeFallback)
    {
        *PartitionInfo = LinuxTypeFallback;
        if (Terminal != nullptr)
        {
            Terminal->printf_("partition scan: selected linux fallback idx=%u\n", LinuxTypeFallback.PartitionIndex);
        }
        return true;
    }

    uint64_t ProbeStartLBA = 2048;
    if (HighestPartitionLastLBA >= ProbeStartLBA)
    {
        ProbeStartLBA = HighestPartitionLastLBA + 1;
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

            if (Terminal != nullptr)
            {
                Terminal->printf_("partition scan: selected ext2 probe fallback start=%lu\n", static_cast<unsigned long>(ProbeLBA));
            }

            return true;
        }
    }

    if (Terminal != nullptr)
    {
        Terminal->printf_("partition scan: ext2 probe fallback exhausted start=%lu limit=%lu\n", static_cast<unsigned long>(ProbeStartLBA),
                          static_cast<unsigned long>(ProbeLimitLBA));
    }

    if (Terminal != nullptr)
    {
        Terminal->printf_("partition scan: no matching Linux filesystem partition found\n");
    }

    return false;
}
