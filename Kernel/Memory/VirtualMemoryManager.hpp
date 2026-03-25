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

    PageMappingFlags(bool UserAccess = false, bool Writeable = true) : UserAccess(UserAccess), Writeable(Writeable)
    {
    }
};

enum CopyPageMapL4FailureStage : uint8_t
{
    COPY_PML4_FAIL_NONE = 0,
    COPY_PML4_FAIL_ALLOC_PML4,
    COPY_PML4_FAIL_ALLOC_PDPT,
    COPY_PML4_FAIL_INVALID_OLD_PDPT,
    COPY_PML4_FAIL_ALLOC_PD,
    COPY_PML4_FAIL_INVALID_OLD_PD,
    COPY_PML4_FAIL_ALLOC_PT,
    COPY_PML4_FAIL_INVALID_OLD_PT,
};

struct CopyPageMapL4DebugInfo
{
    CopyPageMapL4FailureStage FailureStage;
    uint16_t                  PML4Index;
    uint16_t                  PDPTIndex;
    uint16_t                  PDIndex;
    uint16_t                  Reserved;
    uint64_t                  SourceEntryValue;
    uint64_t                  DerivedAddress;
    uint64_t                  AllocationAttempts;
};

enum PageTableMutationEvent : uint8_t
{
    PAGE_TABLE_MUTATION_NONE = 0,
    PAGE_TABLE_MUTATION_WRITE_PML4,
    PAGE_TABLE_MUTATION_WRITE_PDPT,
    PAGE_TABLE_MUTATION_WRITE_PD,
    PAGE_TABLE_MUTATION_WRITE_PT,
    PAGE_TABLE_MUTATION_OBSERVE_PD_NONCANONICAL,
};

struct PageTableMutationDebugInfo
{
    PageTableMutationEvent Event;
    uint16_t               PML4Index;
    uint16_t               PDPTIndex;
    uint16_t               PDIndex;
    uint16_t               PTIndex;
    uint64_t               EntryValue;
    uint64_t               DerivedAddress;
};

class VirtualMemoryManager
{
private:
    PageTableEntry*        PageMapL4Table;
    PhysicalMemoryManager& PMM;
    CopyPageMapL4DebugInfo LastCopyPageMapL4DebugInfo;
    PageTableMutationDebugInfo LastPageTableMutationDebugInfo;

public:
    VirtualMemoryManager(UINTN PageMapL4TableAddr, PhysicalMemoryManager& PMM);
    bool            MapPage(UINTN PhysicalAddr, UINTN VirtualAddr, const PageMappingFlags& Flags);
    bool            ProtectPage(UINTN VirtualAddr, bool UserAccess, bool Writeable, bool Executable);
    bool            UnmapPage(UINTN VirtualAddr);
    UINTN           MapRange(UINTN PhysicalAddr, UINTN VirtualAddr, UINTN Pages, const PageMappingFlags& Flags);
    UINTN           ProtectRange(UINTN VirtualAddr, UINTN Pages, bool UserAccess, bool Writeable, bool Executable);
    UINTN           UnmapRange(UINTN VirtualAddr, UINTN Pages);
    PageTableEntry* CopyPageMapL4Table();
    const CopyPageMapL4DebugInfo& GetLastCopyPageMapL4DebugInfo() const;
    const PageTableMutationDebugInfo& GetLastPageTableMutationDebugInfo() const;
};
