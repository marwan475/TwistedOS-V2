#pragma once

#include <Console.hpp>
#include <uefi.hpp>

#define PAGE_SIZE 4096
#define PHYS_PAGE_ADDR_MASK 0x000FFFFFFFFFF000

#define KERNEL_BASE_VIRTUAL_ADDR 0xFFFFFFFF80000000
#define KERNEL_STACK_SIZE (16 * PAGE_SIZE)

typedef union
{
    uint64_t value; // full 64-bit virtual address
    struct
    {
        uint64_t offset : 12;    // Page offset within 4KB page
        uint64_t pt_index : 9;   // Page Table index
        uint64_t pd_index : 9;   // Page Directory index
        uint64_t pdpt_index : 9; // Page Directory Pointer index
        uint64_t pml4_index : 9; // PML4 index
        uint64_t reserved : 16;  // Upper bits (sign extension)
    } __attribute__((__packed__)) fields;
} VirtualAddress;

typedef union
{
    uint64_t value;
    struct
    {
        uint64_t present : 1;            // Page is present
        uint64_t writeable : 1;          // Writable
        uint64_t user_access : 1;        // User-mode accessible
        uint64_t write_through : 1;      // Write-through caching
        uint64_t cache_disabled : 1;     // Disable caching
        uint64_t accessed : 1;           // Accessed by CPU
        uint64_t dirty : 1;              // Written to
        uint64_t size : 1;               // 1 = 2MB/1GB page, 0 = 4KB page
        uint64_t global : 1;             // Global page
        uint64_t ignored_2 : 3;          // Ignored by CPU
        uint64_t page_ppn : 28;          // Physical page number (shifted >> 12)
        uint64_t reserved_1 : 12;        // Must be 0
        uint64_t ignored_1 : 11;         // Ignored by CPU
        uint64_t execution_disabled : 1; // NX bit
    } __attribute__((__packed__)) fields;
} PageTableEntry;

typedef struct
{
    UINTN                  MemoryMapSize;
    EFI_MEMORY_DESCRIPTOR* MemoryMap;
    UINTN                  MapKey;
    UINTN                  DescriptorSize;
    UINT32                 DescriptorVersion;
} MemoryMapInfo;

void kmemset(void* dest, int value, size_t count);

class MemoryManager
{
private:
    MemoryMapInfo   MemoryMap;
    PageTableEntry* PageMapL4Table;
    void*           NextPageAddress;
    UINTN           CurrentDescriptor;
    UINTN           RemainingPagesInDescriptor;

public:
    MemoryManager(MemoryMapInfo MemoryMap);
    ~MemoryManager();
    void* AllocateAvailablePagesFromMemoryMap(UINTN Pages);
    bool  MapPage(UINTN PysicalAddr, UINTN VirtualAddr);
    bool  UnmapPage(UINTN VirtualAddr);
    bool  IdentityMapPage(UINTN VirtualAddr);
    UINTN IdentityMapRange(UINTN BaseAddr, UINTN Size);
    UINTN MapRange(UINTN PhysicalAddr, UINTN VirtualAddr, UINTN Pages);
    UINTN MapKernelToHigherHalf(UINTN PhysicalAddr, UINTN Size);
    void  IdentityMapMemoryMap();
    void  InitPaging();
};
