#include "RamFileSystemManager.hpp"

#include <Logging/FrameBufferConsole.hpp>

namespace
{
constexpr uint64_t CpioModeFileTypeMask = 0170000;
constexpr uint64_t CpioModeRegularFile  = 0100000;
constexpr uint64_t CpioModeDirectory    = 0040000;

struct CpioNewcHeader
{
    char Magic[6];
    char Inode[8];
    char Mode[8];
    char Uid[8];
    char Gid[8];
    char NLink[8];
    char MTime[8];
    char FileSize[8];
    char DevMajor[8];
    char DevMinor[8];
    char RDevMajor[8];
    char RDevMinor[8];
    char NameSize[8];
    char Check[8];
};

static_assert(sizeof(CpioNewcHeader) == 110, "cpio newc header size mismatch");

bool IsHexDigit(char Character)
{
    return (Character >= '0' && Character <= '9') || (Character >= 'a' && Character <= 'f') || (Character >= 'A' && Character <= 'F');
}

uint64_t ParseHexField(const char* Field, uint64_t Length, bool& Valid)
{
    uint64_t Value = 0;

    for (uint64_t Index = 0; Index < Length; ++Index)
    {
        char Character = Field[Index];
        if (!IsHexDigit(Character))
        {
            Valid = false;
            return 0;
        }

        Value <<= 4;

        if (Character >= '0' && Character <= '9')
        {
            Value |= static_cast<uint64_t>(Character - '0');
        }
        else if (Character >= 'a' && Character <= 'f')
        {
            Value |= static_cast<uint64_t>(Character - 'a' + 10);
        }
        else
        {
            Value |= static_cast<uint64_t>(Character - 'A' + 10);
        }
    }

    return Value;
}

bool MatchesLiteral(const char* Value, const char* Literal, uint64_t Length)
{
    for (uint64_t Index = 0; Index < Length; ++Index)
    {
        if (Value[Index] != Literal[Index])
        {
            return false;
        }
    }

    return true;
}

uint64_t AlignUpToFour(uint64_t Value)
{
    return (Value + 3) & ~static_cast<uint64_t>(3);
}

RamFileSystemEntryType DecodeEntryType(uint64_t Mode)
{
    uint64_t FileTypeBits = Mode & CpioModeFileTypeMask;

    if (FileTypeBits == CpioModeRegularFile)
    {
        return RamFileSystemEntryTypeRegularFile;
    }

    if (FileTypeBits == CpioModeDirectory)
    {
        return RamFileSystemEntryTypeDirectory;
    }

    if (FileTypeBits == 0)
    {
        return RamFileSystemEntryTypeUnknown;
    }

    return RamFileSystemEntryTypeOther;
}

const char* EntryTypeToString(RamFileSystemEntryType Type)
{
    switch (Type)
    {
        case RamFileSystemEntryTypeRegularFile:
            return "file";
        case RamFileSystemEntryTypeDirectory:
            return "directory";
        case RamFileSystemEntryTypeOther:
            return "other";
        case RamFileSystemEntryTypeUnknown:
        default:
            return "unknown";
    }
}

const char* SkipPathPrefixes(const char* Path)
{
    if (Path == nullptr)
    {
        return nullptr;
    }

    while (*Path == '/')
    {
        ++Path;
    }

    while (Path[0] == '.' && Path[1] == '/')
    {
        Path += 2;
    }

    return Path;
}

bool StringsEqual(const char* Left, const char* Right)
{
    if (Left == nullptr || Right == nullptr)
    {
        return false;
    }

    while (*Left != '\0' && *Right != '\0')
    {
        if (*Left != *Right)
        {
            return false;
        }

        ++Left;
        ++Right;
    }

    return *Left == '\0' && *Right == '\0';
}

bool PathMatchesArchiveName(const char* RequestedPath, const char* ArchiveName)
{
    const char* NormalizedRequested = SkipPathPrefixes(RequestedPath);
    const char* NormalizedArchive   = SkipPathPrefixes(ArchiveName);

    if (NormalizedRequested == nullptr || NormalizedArchive == nullptr)
    {
        return false;
    }

    if (*NormalizedRequested == '\0' && ArchiveName[0] == '.' && ArchiveName[1] == '\0')
    {
        return true;
    }

    return StringsEqual(NormalizedRequested, NormalizedArchive);
}

bool ParseEntryAtOffset(const char* Base, uint64_t ArchiveSize, uint64_t Offset, RamFileSystemEntry* Entry, uint64_t* NextOffset, bool* ReachedTrailer, FrameBufferConsole* Console)
{
    const CpioNewcHeader* Header = reinterpret_cast<const CpioNewcHeader*>(Base + Offset);
    if (!MatchesLiteral(Header->Magic, "070701", 6) && !MatchesLiteral(Header->Magic, "070702", 6))
    {
        if (Console != nullptr)
        {
            Console->printf_("Initramfs parse error: invalid cpio magic at offset %llu\n", static_cast<unsigned long long>(Offset));
        }

        return false;
    }

    bool     Valid    = true;
    uint64_t NameSize = ParseHexField(Header->NameSize, 8, Valid);
    uint64_t FileSize = ParseHexField(Header->FileSize, 8, Valid);
    uint64_t Mode     = ParseHexField(Header->Mode, 8, Valid);

    if (!Valid || NameSize == 0)
    {
        if (Console != nullptr)
        {
            Console->printf_("Initramfs parse error: invalid header fields at offset %llu\n", static_cast<unsigned long long>(Offset));
        }

        return false;
    }

    uint64_t NameOffset     = Offset + sizeof(CpioNewcHeader);
    uint64_t NameEndOffset  = NameOffset + NameSize;
    uint64_t AlignedNameEnd = AlignUpToFour(NameEndOffset);
    uint64_t DataEndOffset  = AlignedNameEnd + FileSize;
    uint64_t EntryEndOffset = AlignUpToFour(DataEndOffset);

    if (NameEndOffset > ArchiveSize || AlignedNameEnd > ArchiveSize || DataEndOffset > ArchiveSize || EntryEndOffset > ArchiveSize)
    {
        if (Console != nullptr)
        {
            Console->printf_("Initramfs parse error: entry exceeds archive bounds at offset %llu\n", static_cast<unsigned long long>(Offset));
        }

        return false;
    }

    const char* Name = Base + NameOffset;
    if (Name[NameSize - 1] != '\0')
    {
        if (Console != nullptr)
        {
            Console->printf_("Initramfs parse error: filename is not null-terminated at offset %llu\n", static_cast<unsigned long long>(Offset));
        }

        return false;
    }

    *ReachedTrailer = MatchesLiteral(Name, "TRAILER!!!", 10);
    *NextOffset     = EntryEndOffset;

    if (*ReachedTrailer)
    {
        return true;
    }

    if (Entry != nullptr)
    {
        Entry->Name = Name;
        Entry->Data = Base + AlignedNameEnd;
        Entry->Size = FileSize;
        Entry->Mode = Mode;
        Entry->Type = DecodeEntryType(Mode);
    }

    return true;
}

typedef struct
{
    FrameBufferConsole* Console;
    uint64_t            EntryCount;
} PrintInitramfsContext;

bool PrintInitramfsEntry(const RamFileSystemEntry& Entry, void* Context)
{
    PrintInitramfsContext* PrintContext = reinterpret_cast<PrintInitramfsContext*>(Context);
    ++PrintContext->EntryCount;

    PrintContext->Console->printf_("initramfs[%llu]: %s [%s] (mode=0x%llx, size=%llu)\n", static_cast<unsigned long long>(PrintContext->EntryCount), Entry.Name, EntryTypeToString(Entry.Type),
                                   static_cast<unsigned long long>(Entry.Mode), static_cast<unsigned long long>(Entry.Size));

    return true;
}

typedef struct
{
    const char*         Path;
    RamFileSystemEntry* Entry;
    bool                Found;
} FindEntryContext;

bool FindEntryByPath(const RamFileSystemEntry& Entry, void* Context)
{
    FindEntryContext* FindContext = reinterpret_cast<FindEntryContext*>(Context);
    if (!PathMatchesArchiveName(FindContext->Path, Entry.Name))
    {
        return true;
    }

    if (FindContext->Entry != nullptr)
    {
        *FindContext->Entry = Entry;
    }

    FindContext->Found = true;
    return false;
}
} // namespace

RamFileSystemManager::RamFileSystemManager(uint64_t InitramfsAddress, uint64_t InitramfsSize) : InitramfsAddress(InitramfsAddress), InitramfsSize(InitramfsSize)
{
}

uint64_t RamFileSystemManager::GetInitramfsAddress() const
{
    return InitramfsAddress;
}

uint64_t RamFileSystemManager::GetInitramfsSize() const
{
    return InitramfsSize;
}

bool RamFileSystemManager::EnumerateEntries(RamFileSystemEntryCallback Callback, void* Context, FrameBufferConsole* Console) const
{
    if (InitramfsAddress == 0 || InitramfsSize < sizeof(CpioNewcHeader))
    {
        if (Console != nullptr)
        {
            Console->printf_("Initramfs archive is empty or invalid\n");
        }

        return false;
    }

    const char* Base   = reinterpret_cast<const char*>(InitramfsAddress);
    uint64_t    Offset = 0;

    while (Offset + sizeof(CpioNewcHeader) <= InitramfsSize)
    {
        RamFileSystemEntry Entry          = {};
        uint64_t           NextOffset     = 0;
        bool               ReachedTrailer = false;

        if (!ParseEntryAtOffset(Base, InitramfsSize, Offset, &Entry, &NextOffset, &ReachedTrailer, Console))
        {
            return false;
        }

        if (ReachedTrailer)
        {
            return true;
        }

        if (Callback != nullptr && !Callback(Entry, Context))
        {
            return true;
        }

        Offset = NextOffset;
    }

    if (Console != nullptr)
    {
        Console->printf_("Initramfs parse error: missing TRAILER!!! entry\n");
    }

    return false;
}

bool RamFileSystemManager::FindEntry(const char* Path, RamFileSystemEntry* Entry, FrameBufferConsole* Console) const
{
    if (Path == nullptr || Entry == nullptr)
    {
        return false;
    }

    FindEntryContext Context = {Path, Entry, false};
    if (!EnumerateEntries(FindEntryByPath, &Context, Console))
    {
        return false;
    }

    return Context.Found;
}

bool RamFileSystemManager::FindFile(const char* Path, const void** Data, uint64_t* Size, FrameBufferConsole* Console) const
{
    if (Path == nullptr || Data == nullptr || Size == nullptr)
    {
        return false;
    }

    RamFileSystemEntry Entry = {};
    if (!FindEntry(Path, &Entry, Console))
    {
        return false;
    }

    if (Entry.Type != RamFileSystemEntryTypeRegularFile)
    {
        if (Console != nullptr)
        {
            Console->printf_("Initramfs entry %s is not a regular file\n", Path);
        }

        return false;
    }

    *Data = Entry.Data;
    *Size = Entry.Size;
    return true;
}

void RamFileSystemManager::ParseAndPrintInitramfs(FrameBufferConsole* Console) const
{
    if (Console == nullptr)
    {
        return;
    }

    Console->printf_("Initramfs archive at %p (%llu bytes)\n", reinterpret_cast<const void*>(InitramfsAddress), static_cast<unsigned long long>(InitramfsSize));

    PrintInitramfsContext Context = {Console, 0};
    if (!EnumerateEntries(PrintInitramfsEntry, &Context, Console))
    {
        return;
    }

    Console->printf_("Initramfs entries: %llu\n", static_cast<unsigned long long>(Context.EntryCount));

    const void* InitData = nullptr;
    uint64_t    InitSize = 0;
    if (FindFile("/init", &InitData, &InitSize, nullptr))
    {
        Console->printf_("Initramfs file /init found at %p (%llu bytes)\n", InitData, static_cast<unsigned long long>(InitSize));
    }
    else
    {
        Console->printf_("Initramfs file /init was not found\n");
    }
}
