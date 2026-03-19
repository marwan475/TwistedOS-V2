/**
 * File: VirtualAddressSpace.hpp
 * Author: Marwan Mostafa
 * Description: Virtual address space management declarations.
 */

#pragma once

#include <Memory/PhysicalMemoryManager.hpp>
#include <Memory/VirtualMemoryManager.hpp>
#include <stddef.h>
#include <stdint.h>

class VirtualAddressSpace
{
private:
    uint64_t CodePhysicalAddress;
    uint64_t CodeSize;
    uint64_t CodeVirtualAddressStart;
    uint64_t HeapPhysicalAddress;
    uint64_t HeapSize;
    uint64_t HeapVirtualAddressStart;
    uint64_t StackPhysicalAddress;
    uint64_t StackSize;
    uint64_t StackVirtualAddressStart;
    uint64_t PageMapL4TableAddr;

protected:
    void SetPageMapL4TableAddr(uint64_t PageMapL4TableAddr);

public:
    VirtualAddressSpace();
    VirtualAddressSpace(uint64_t CodePhysicalAddress, uint64_t CodeSize, uint64_t CodeVirtualAddressStart, uint64_t HeapPhysicalAddress, uint64_t HeapSize, uint64_t HeapVirtualAddressStart,
                        uint64_t StackPhysicalAddress, uint64_t StackSize, uint64_t StackVirtualAddressStart);
    virtual ~VirtualAddressSpace();

    uint64_t GetCodePhysicalAddress() const;
    uint64_t GetCodeSize() const;
    uint64_t GetCodeVirtualAddressStart() const;

    uint64_t GetHeapPhysicalAddress() const;
    uint64_t GetHeapSize() const;
    uint64_t GetHeapVirtualAddressStart() const;
    void     SetHeapPhysicalAddress(uint64_t HeapPhysicalAddress);
    void     SetHeapSize(uint64_t HeapSize);

    uint64_t GetStackPhysicalAddress() const;
    uint64_t GetStackSize() const;
    uint64_t GetStackVirtualAddressStart() const;
    uint64_t GetStackTop() const;

    uint64_t GetPageMapL4TableAddr() const;

    virtual bool Init(uint64_t PageMapL4TableAddr, PhysicalMemoryManager& PMM);
};

struct ELFMemoryRegion
{
    uint64_t PhysicalAddress;
    uint64_t VirtualAddress;
    uint64_t Size;
    bool     Writable;
};

class VirtualAddressSpaceELF : public VirtualAddressSpace
{
private:
    static constexpr size_t MaxMemoryRegions = 16;

    ELFMemoryRegion MemoryRegions[MaxMemoryRegions];
    size_t          MemoryRegionCount;

public:
    VirtualAddressSpaceELF();
    VirtualAddressSpaceELF(uint64_t CodePhysicalAddress, uint64_t CodeSize, uint64_t CodeVirtualAddressStart, uint64_t HeapPhysicalAddress, uint64_t HeapSize, uint64_t HeapVirtualAddressStart,
                           uint64_t StackPhysicalAddress, uint64_t StackSize, uint64_t StackVirtualAddressStart);
    ~VirtualAddressSpaceELF() override;

    const ELFMemoryRegion* GetMemoryRegions() const;
    size_t                 GetMemoryRegionCount() const;
    bool                   AddMemoryRegion(const ELFMemoryRegion& Region);
    bool                   Init(uint64_t PageMapL4TableAddr, PhysicalMemoryManager& PMM) override;
};
