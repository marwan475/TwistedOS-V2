/**
 * File: KernelHeapManager.hpp
 * Author: Marwan Mostafa
 * Description: Kernel heap allocation manager declarations.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define KERNEL_HEAP_SIZE (16 * PAGE_SIZE)
#define BLOCK_SIZE 64
#define TOTAL_BLOCKS (KERNEL_HEAP_SIZE / BLOCK_SIZE)
#define BITS_PER_BYTE 8
#define BITMAP_SIZE (TOTAL_BLOCKS / BITS_PER_BYTE)

// Way to track free memory
// - Bitmap
// Way to track allocations
// - Header before allocation with size and magic

// Slow bitmap allocator for the kernel heap. We can optimize this later if needed
class KernelHeapManager
{
    struct AllocationHeader
    {
        uint32_t BlockCount;
        uint32_t Magic;
    };

    uint64_t HeapStart;
    uint64_t HeapEnd;
    uint8_t  BitMap[BITMAP_SIZE];

    size_t GetManagedHeapSize() const;
    size_t GetUsableBlockCount() const;
    bool   IsBlockUsed(size_t BlockIndex) const;
    void   SetBlockUsed(size_t BlockIndex, bool Used);
    bool   IsRangeFree(size_t StartBlock, size_t BlockCount) const;
    void   MarkRange(size_t StartBlock, size_t BlockCount, bool Used);

public:
    KernelHeapManager(uint64_t HeapStart, uint64_t HeapEnd);
    void* kmalloc(size_t Size);
    void  kfree(void* Ptr);
};