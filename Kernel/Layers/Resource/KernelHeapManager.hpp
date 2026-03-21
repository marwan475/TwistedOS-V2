/**
 * File: KernelHeapManager.hpp
 * Author: Marwan Mostafa
 * Description: Kernel heap allocation manager declarations.
 */

#pragma once

#include <KernelParameters.hpp>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_HEAP_SIZE (KERNEL_HEAP_PAGES * KERNEL_PAGE_SIZE)
#define SLAB_PAGE_SIZE KERNEL_PAGE_SIZE

class KernelHeapManager
{
    static constexpr size_t SLAB_CLASS_COUNT = 7;
    static constexpr size_t MAX_SLAB_COUNT   = ((KERNEL_HEAP_SIZE / SLAB_PAGE_SIZE) > 0) ? (KERNEL_HEAP_SIZE / SLAB_PAGE_SIZE) : 1;

    struct AllocationHeader
    {
        uint32_t Magic;
        uint32_t SlabIndex;
        uint16_t ClassIndex;
        uint16_t Reserved;
    };

    struct FreeObject
    {
        FreeObject* Next;
    };

    enum SlabState : uint8_t
    {
        SlabFree = 0,
        SlabSmall,
        SlabLargeHead,
        SlabLargeTail,
    };

    struct SlabDescriptor
    {
        uint8_t     State;
        uint8_t     ClassIndex;
        uint16_t    Reserved;
        uint32_t    OwnerOrSpan;
        uint32_t    TotalObjects;
        uint32_t    FreeObjects;
        FreeObject* FreeList;
        int32_t     PrevAvailable;
        int32_t     NextAvailable;
    };

    uint64_t HeapStart;
    uint64_t HeapEnd;
    size_t   ManagedHeapSize;
    size_t   SlabCount;
    SlabDescriptor Slabs[MAX_SLAB_COUNT];
    int32_t  ClassAvailableHead[SLAB_CLASS_COUNT];

    size_t   GetManagedHeapSize() const;
    size_t   GetEffectiveSlabCount() const;
    size_t   SelectClassIndex(size_t Size) const;
    size_t   GetClassObjectSize(size_t ClassIndex) const;
    uintptr_t GetSlabBaseAddress(size_t SlabIndex) const;
    void     ResetAllocatorState();
    void     LinkSlabIntoClassList(size_t SlabIndex, size_t ClassIndex);
    void     UnlinkSlabFromClassList(size_t SlabIndex, size_t ClassIndex);
    bool     InitializeSmallSlab(size_t SlabIndex, size_t ClassIndex);
    int64_t  FindFreeSlabRun(size_t SlabSpan) const;
    void     ReleaseSlabRun(size_t StartSlab, size_t SlabSpan);

public:
    KernelHeapManager(uint64_t HeapStart, uint64_t HeapEnd);
    void* kmalloc(size_t Size);
    void  kfree(void* Ptr);
};