#pragma once

#include <KernelParameters.hpp>

#define PAGE_SIZE 4096

class PhysicalMemoryManager
{
private:
    MemoryMapInfo MemoryMap;
    UINTN         NextPageAddress;
    UINTN         CurrentDescriptor;
    UINTN         RemainingPagesInDescriptor;

public:
    PhysicalMemoryManager(MemoryMapInfo MemoryMap, UINTN NextPageAddress, UINTN CurrentDescriptor,
                          UINTN RemainingPagesInDescriptor);

    void* AllocatePages(UINTN Pages);
    UINTN GetTotalUsableMemory() const;

    UINTN GetNextPageAddress() const;
    UINTN GetCurrentDescriptor() const;
    UINTN GetRemainingPagesInDescriptor() const;
};
