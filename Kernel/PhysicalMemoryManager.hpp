#pragma once

#include <KernelParameters.hpp>

#define PAGE_SIZE 4096

class FrameBufferConsole;

typedef struct
{
    uint64_t PhysicalAddressStart;
    uint64_t TotalNumberOfPages;
    uint64_t NumberOfFreePages;
    uint8_t* BitMap;
} MemoryDescriptor;

class PhysicalMemoryManager
{
private:
    MemoryMapInfo    MemoryMap;
    UINTN            NextPageAddress;
    UINTN            CurrentDescriptor;
    UINTN            RemainingPagesInDescriptor;
    UINTN            TotalUsableMemory;
    UINTN            TotalNumberOfPages;
    MemoryDescriptor MemoryDescriptorInfo;

    void InitializeMemoryStats();

public:
    PhysicalMemoryManager(MemoryMapInfo MemoryMap, UINTN NextPageAddress, UINTN CurrentDescriptor,
                          UINTN RemainingPagesInDescriptor);

    void* AllocatePagesFromMemoryMap(UINTN Pages);
    UINTN TotalUsableMemoryBytes() const;
    UINTN TotalPages() const;
    void  PrintConventionalMemoryMap(FrameBufferConsole& Console) const;
    void  InitializeMemoryDescriptors();
    void  PrintMemoryDescriptors(FrameBufferConsole& Console) const;
};
