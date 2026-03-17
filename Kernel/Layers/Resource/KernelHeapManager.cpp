#include "KernelHeapManager.hpp"

#include <stddef.h>

namespace
{
constexpr uint32_t ALLOCATION_MAGIC = 0x4B484D47;
}

/**
 * Function: KernelHeapManager::KernelHeapManager
 * Description: Initializes kernel heap range metadata and clears allocation bitmap.
 * Parameters:
 *   uint64_t HeapStart - Start address of managed heap range.
 *   uint64_t HeapEnd - End address of managed heap range.
 * Returns:
 *   KernelHeapManager - Constructed kernel heap manager instance.
 */
KernelHeapManager::KernelHeapManager(uint64_t HeapStart, uint64_t HeapEnd) : HeapStart(HeapStart), HeapEnd(HeapEnd)
{
    if (this->HeapStart != 0)
    {
        uint64_t ExpectedEnd = this->HeapStart + static_cast<uint64_t>(KERNEL_HEAP_SIZE);
        if (ExpectedEnd > this->HeapStart)
        {
            if (this->HeapEnd <= this->HeapStart)
            {
                this->HeapEnd = ExpectedEnd;
            }
            else
            {
                uint64_t RangeSize = this->HeapEnd - this->HeapStart;
                if (RangeSize > static_cast<uint64_t>(KERNEL_HEAP_SIZE))
                {
                    this->HeapEnd = ExpectedEnd;
                }
            }
        }
    }

    for (size_t Index = 0; Index < BITMAP_SIZE; ++Index)
    {
        BitMap[Index] = 0;
    }
}

/**
 * Function: KernelHeapManager::GetManagedHeapSize
 * Description: Returns the effective heap size managed by this allocator.
 * Parameters:
 *   None
 * Returns:
 *   size_t - Number of bytes managed by the heap manager.
 */
size_t KernelHeapManager::GetManagedHeapSize() const
{
    if (HeapEnd <= HeapStart)
    {
        return (HeapStart == 0) ? 0 : KERNEL_HEAP_SIZE;
    }

    size_t RangeSize = static_cast<size_t>(HeapEnd - HeapStart);
    return (RangeSize < KERNEL_HEAP_SIZE) ? RangeSize : KERNEL_HEAP_SIZE;
}

/**
 * Function: KernelHeapManager::GetUsableBlockCount
 * Description: Returns the number of fixed-size blocks available for allocations.
 * Parameters:
 *   None
 * Returns:
 *   size_t - Usable block count.
 */
size_t KernelHeapManager::GetUsableBlockCount() const
{
    return GetManagedHeapSize() / BLOCK_SIZE;
}

/**
 * Function: KernelHeapManager::IsBlockUsed
 * Description: Checks whether a specific block is marked as allocated.
 * Parameters:
 *   size_t BlockIndex - Index of the block to query.
 * Returns:
 *   bool - True if block is used, false if free.
 */
bool KernelHeapManager::IsBlockUsed(size_t BlockIndex) const
{
    size_t ByteIndex = BlockIndex / BITS_PER_BYTE;
    size_t BitIndex  = BlockIndex % BITS_PER_BYTE;
    return (BitMap[ByteIndex] & (1u << BitIndex)) != 0;
}

/**
 * Function: KernelHeapManager::SetBlockUsed
 * Description: Marks or unmarks a block as allocated in the allocation bitmap.
 * Parameters:
 *   size_t BlockIndex - Index of the block to modify.
 *   bool Used - True to mark used, false to mark free.
 * Returns:
 *   void - No return value.
 */
void KernelHeapManager::SetBlockUsed(size_t BlockIndex, bool Used)
{
    size_t  ByteIndex = BlockIndex / BITS_PER_BYTE;
    size_t  BitIndex  = BlockIndex % BITS_PER_BYTE;
    uint8_t Mask      = static_cast<uint8_t>(1u << BitIndex);

    if (Used)
    {
        BitMap[ByteIndex] |= Mask;
        return;
    }

    BitMap[ByteIndex] &= static_cast<uint8_t>(~Mask);
}

/**
 * Function: KernelHeapManager::IsRangeFree
 * Description: Tests whether a contiguous block range is entirely free.
 * Parameters:
 *   size_t StartBlock - Index of first block in range.
 *   size_t BlockCount - Number of blocks in range.
 * Returns:
 *   bool - True if all blocks in range are free, false otherwise.
 */
bool KernelHeapManager::IsRangeFree(size_t StartBlock, size_t BlockCount) const
{
    for (size_t Offset = 0; Offset < BlockCount; ++Offset)
    {
        if (IsBlockUsed(StartBlock + Offset))
        {
            return false;
        }
    }

    return true;
}

/**
 * Function: KernelHeapManager::MarkRange
 * Description: Marks all blocks in a range as used or free.
 * Parameters:
 *   size_t StartBlock - Index of first block in range.
 *   size_t BlockCount - Number of blocks in range.
 *   bool Used - True to mark used, false to mark free.
 * Returns:
 *   void - No return value.
 */
void KernelHeapManager::MarkRange(size_t StartBlock, size_t BlockCount, bool Used)
{
    for (size_t Offset = 0; Offset < BlockCount; ++Offset)
    {
        SetBlockUsed(StartBlock + Offset, Used);
    }
}

/**
 * Function: KernelHeapManager::kmalloc
 * Description: Allocates a heap block range large enough for the requested size and allocation header.
 * Parameters:
 *   size_t Size - Number of user bytes requested.
 * Returns:
 *   void* - Pointer to usable allocation memory, or nullptr on failure.
 */
void* KernelHeapManager::kmalloc(size_t Size)
{
    if (Size == 0)
    {
        return nullptr;
    }

    size_t UsableBlocks = GetUsableBlockCount();
    if (UsableBlocks == 0)
    {
        return nullptr;
    }

    if (Size > (SIZE_MAX - sizeof(AllocationHeader)))
    {
        return nullptr;
    }

    size_t TotalBytesNeeded = Size + sizeof(AllocationHeader);
    size_t BlocksNeeded     = (TotalBytesNeeded + (BLOCK_SIZE - 1)) / BLOCK_SIZE;

    if (BlocksNeeded == 0 || BlocksNeeded > UsableBlocks)
    {
        return nullptr;
    }

    for (size_t BlockIndex = 0; BlockIndex + BlocksNeeded <= UsableBlocks; ++BlockIndex)
    {
        if (!IsRangeFree(BlockIndex, BlocksNeeded))
        {
            continue;
        }

        MarkRange(BlockIndex, BlocksNeeded, true);

        uintptr_t         HeaderAddress = static_cast<uintptr_t>(HeapStart) + (BlockIndex * BLOCK_SIZE);
        AllocationHeader* Header        = reinterpret_cast<AllocationHeader*>(HeaderAddress);
        Header->BlockCount              = static_cast<uint32_t>(BlocksNeeded);
        Header->Magic                   = ALLOCATION_MAGIC;

        return reinterpret_cast<void*>(HeaderAddress + sizeof(AllocationHeader));
    }

    return nullptr;
}

/**
 * Function: KernelHeapManager::kfree
 * Description: Frees a previously allocated heap block range after validating allocation metadata.
 * Parameters:
 *   void* Ptr - Pointer returned by kmalloc.
 * Returns:
 *   void - No return value.
 */
void KernelHeapManager::kfree(void* Ptr)
{
    if (Ptr == nullptr)
    {
        return;
    }

    uintptr_t UserAddress = reinterpret_cast<uintptr_t>(Ptr);
    if (UserAddress < (static_cast<uintptr_t>(HeapStart) + sizeof(AllocationHeader)))
    {
        return;
    }

    uintptr_t HeaderAddress = UserAddress - sizeof(AllocationHeader);
    if (HeaderAddress < static_cast<uintptr_t>(HeapStart) || HeaderAddress >= static_cast<uintptr_t>(HeapEnd))
    {
        return;
    }

    if ((HeaderAddress - static_cast<uintptr_t>(HeapStart)) % BLOCK_SIZE != 0)
    {
        return;
    }

    AllocationHeader* Header = reinterpret_cast<AllocationHeader*>(HeaderAddress);
    if (Header->Magic != ALLOCATION_MAGIC)
    {
        return;
    }

    size_t BlockCount   = static_cast<size_t>(Header->BlockCount);
    size_t UsableBlocks = GetUsableBlockCount();
    size_t StartBlock   = (HeaderAddress - static_cast<uintptr_t>(HeapStart)) / BLOCK_SIZE;

    if (BlockCount == 0 || StartBlock + BlockCount > UsableBlocks)
    {
        return;
    }

    Header->Magic = 0;
    MarkRange(StartBlock, BlockCount, false);
}
