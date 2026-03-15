#pragma once

#include <stdint.h>

class FrameBufferConsole;

enum RamFileSystemEntryType
{
    RamFileSystemEntryTypeUnknown = 0,
    RamFileSystemEntryTypeRegularFile,
    RamFileSystemEntryTypeDirectory,
    RamFileSystemEntryTypeOther
};

typedef struct
{
    const char*            Name;
    const void*            Data;
    uint64_t               Size;
    uint64_t               Mode;
    RamFileSystemEntryType Type;
} RamFileSystemEntry;

typedef bool (*RamFileSystemEntryCallback)(const RamFileSystemEntry& Entry, void* Context);

class RamFileSystemManager
{
private:
    uint64_t InitramfsAddress;
    uint64_t InitramfsSize;

public:
    RamFileSystemManager(uint64_t InitramfsAddress, uint64_t InitramfsSize);

    uint64_t GetInitramfsAddress() const;
    uint64_t GetInitramfsSize() const;
    bool     EnumerateEntries(RamFileSystemEntryCallback Callback, void* Context, FrameBufferConsole* Console = nullptr) const;
    bool     FindEntry(const char* Path, RamFileSystemEntry* Entry, FrameBufferConsole* Console = nullptr) const;
    bool     FindFile(const char* Path, const void** Data, uint64_t* Size, FrameBufferConsole* Console = nullptr) const;
    void     ParseAndPrintInitramfs(FrameBufferConsole* Console) const;
};