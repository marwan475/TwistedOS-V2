#pragma once

#include <Memory/PhysicalMemoryManager.hpp>
#include <Memory/VirtualMemoryManager.hpp>
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

public:
    VirtualAddressSpace();
    VirtualAddressSpace(uint64_t CodePhysicalAddress, uint64_t CodeSize, uint64_t CodeVirtualAddressStart, uint64_t HeapPhysicalAddress, uint64_t HeapSize, uint64_t HeapVirtualAddressStart,
                        uint64_t StackPhysicalAddress, uint64_t StackSize, uint64_t StackVirtualAddressStart);

    uint64_t GetCodePhysicalAddress() const;
    uint64_t GetCodeSize() const;
    uint64_t GetCodeVirtualAddressStart() const;

    uint64_t GetHeapPhysicalAddress() const;
    uint64_t GetHeapSize() const;
    uint64_t GetHeapVirtualAddressStart() const;

    uint64_t GetStackPhysicalAddress() const;
    uint64_t GetStackSize() const;
    uint64_t GetStackVirtualAddressStart() const;
    uint64_t GetStackTop() const;

    uint64_t GetPageMapL4TableAddr() const;

    bool Init(uint64_t PageMapL4TableAddr, PhysicalMemoryManager& PMM);
};
