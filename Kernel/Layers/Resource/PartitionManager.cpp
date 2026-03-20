/**
 * File: PartitionManager.cpp
 * Author: Marwan Mostafa
 * Description: Disk partition discovery implementation.
 */

#include "PartitionManager.hpp"

#include "DeviceManager.hpp"
#include "Drivers/IDEController.hpp"

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
} // namespace

bool PartitionManager::LocateRootFileSystemPartition(const DeviceManager* DeviceManagerInstance, RootFileSystemPartitionInfo* PartitionInfo)
{
    if (PartitionInfo == nullptr || DeviceManagerInstance == nullptr)
    {
        return false;
    }

    *PartitionInfo = {};

    IDEController* Controller = DeviceManagerInstance->GetPrimaryIDEController();
    if (Controller == nullptr || !Controller->IsInitialized())
    {
        return false;
    }

    uint8_t SectorBuffer[512] = {};
    if (!DeviceManagerInstance->ReadBlock(1, SectorBuffer))
    {
        return false;
    }

    const GptHeader* Header = reinterpret_cast<const GptHeader*>(SectorBuffer);
    if (!SignatureMatchesGpt(Header->Signature) || Header->SizeOfPartitionEntry < sizeof(GptPartitionEntryPrefix) || Header->NumberOfPartitionEntries == 0)
    {
        return false;
    }

    uint32_t EntryCount = Header->NumberOfPartitionEntries;
    if (EntryCount > 256)
    {
        EntryCount = 256;
    }

    RootFileSystemPartitionInfo LinuxTypeFallback = {};
    bool HasLinuxTypeFallback = false;

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
            return false;
        }

        const GptPartitionEntryPrefix* Entry = reinterpret_cast<const GptPartitionEntryPrefix*>(SectorBuffer + EntryOffset);

        if (IsZeroGuid(Entry->TypeGuid))
        {
            continue;
        }

        if (!GuidMatchesLinuxFilesystemType(Entry->TypeGuid) || Entry->LastLBA < Entry->FirstLBA)
        {
            continue;
        }

        RootFileSystemPartitionInfo Candidate = {};
        Candidate.StartLBA       = Entry->FirstLBA;
        Candidate.SectorCount    = (Entry->LastLBA - Entry->FirstLBA) + 1;
        Candidate.PartitionIndex = EntryIndex + 1;

        if (PartitionEntryNameMatchesRootFs(Entry))
        {
            *PartitionInfo = Candidate;
            return true;
        }

        if (!HasLinuxTypeFallback)
        {
            LinuxTypeFallback    = Candidate;
            HasLinuxTypeFallback = true;
        }
    }

    if (HasLinuxTypeFallback)
    {
        *PartitionInfo = LinuxTypeFallback;
        return true;
    }

    return false;
}
