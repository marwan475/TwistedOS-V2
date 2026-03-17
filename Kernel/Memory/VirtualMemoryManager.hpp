/**
 * File: VirtualMemoryManager.hpp
 * Author: Marwan Mostafa
 * Description: Virtual memory paging management declarations.
 */

#pragma once

#include <Memory/PhysicalMemoryManager.hpp>

#define PHYS_PAGE_ADDR_MASK 0x000FFFFFFFFFF000

typedef union
{
    uint64_t value;
    struct
    {
        uint64_t offset : 12;
        uint64_t pt_index : 9;
        uint64_t pd_index : 9;
        uint64_t pdpt_index : 9;
        uint64_t pml4_index : 9;
        uint64_t reserved : 16;
    } __attribute__((__packed__)) fields;
} VirtualAddress;

typedef union
{
    uint64_t value;
    struct
    {
        uint64_t present : 1;
        uint64_t writeable : 1;
        uint64_t user_access : 1;
        uint64_t write_through : 1;
        uint64_t cache_disabled : 1;
        uint64_t accessed : 1;
        uint64_t dirty : 1;
        uint64_t size : 1;
        uint64_t global : 1;
        uint64_t ignored_2 : 3;
        uint64_t page_ppn : 28;
        uint64_t reserved_1 : 12;
        uint64_t ignored_1 : 11;
        uint64_t execution_disabled : 1;
    } __attribute__((__packed__)) fields;
} PageTableEntry;

struct PageMappingFlags
{
    bool UserAccess;
    bool Writeable;

    PageMappingFlags(bool UserAccess = false, bool Writeable = true)
        : UserAccess(UserAccess), Writeable(Writeable)
    {
    }
};

class VirtualMemoryManager
{
private:
    PageTableEntry*        PageMapL4Table;
    PhysicalMemoryManager& PMM;

public:
    VirtualMemoryManager(UINTN PageMapL4TableAddr, PhysicalMemoryManager& PMM);
    bool            MapPage(UINTN PhysicalAddr, UINTN VirtualAddr, const PageMappingFlags& Flags);
    bool            UnmapPage(UINTN VirtualAddr);
    UINTN           MapRange(UINTN PhysicalAddr, UINTN VirtualAddr, UINTN Pages, const PageMappingFlags& Flags);
    UINTN           UnmapRange(UINTN VirtualAddr, UINTN Pages);
    PageTableEntry* CopyPageMapL4Table();
};
