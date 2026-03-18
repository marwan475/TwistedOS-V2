/**
 * File: PhysicalMemoryManager.hpp
 * Author: Marwan Mostafa
 * Description: Physical memory map and frame management declarations.
 */

#pragma once

#include "../utils/KernelParameters.hpp"

#define PAGE_SIZE 4096

class FrameBufferConsole;

typedef struct
{
    uint64_t PhysicalAddressStart;
    uint64_t TotalNumberOfPages;
    uint64_t NumberOfFreePages;
    uint8_t* BitMap;
} MemoryDescriptor;

struct BuddyBlock
{
    BuddyBlock* Next;
};

class PhysicalMemoryManager
{
private:
    static constexpr uint8_t MAX_BUDDY_ORDERS = 52;

    MemoryMapInfo    MemoryMap;
    UINTN            NextPageAddress;
    UINTN            CurrentDescriptor;
    UINTN            RemainingPagesInDescriptor;
    UINTN            TotalUsableMemory;
    UINTN            TotalNumberOfPages;
    MemoryDescriptor MemoryDescriptorInfo;
    BuddyBlock*      BuddyFreeLists[MAX_BUDDY_ORDERS];
    uint8_t          BuddyMaxOrder;

    void InitializeMemoryStats();
    void InitializeBuddyAllocator();

    static uint8_t  CeilLog2Pages(uint64_t Pages);
    static uint8_t  FloorLog2Pages(uint64_t Pages);
    static uint64_t PagesForOrder(uint8_t Order);
    bool            RemoveBlockFromFreeList(uint8_t Order, uint64_t BlockAddress);
    bool            IsAddressInManagedRange(uint64_t Address) const;

public:
    PhysicalMemoryManager(MemoryMapInfo MemoryMap, UINTN NextPageAddress, UINTN CurrentDescriptor, UINTN RemainingPagesInDescriptor);

    void* AllocatePagesFromMemoryMap(UINTN Pages);
    void* AllocatePagesFromDescriptor(UINTN Pages);
    bool  FreePagesFromDescriptor(void* Address, UINTN Pages);
    UINTN TotalUsableMemoryBytes() const;
    UINTN TotalPages() const;
    void  PrintConventionalMemoryMap(FrameBufferConsole& Console) const;
    void  InitializeMemoryDescriptors();
    void  PrintMemoryDescriptors(FrameBufferConsole& Console) const;
};
