/**
 * File: KernelHeapManager.cpp
 * Author: Marwan Mostafa
 * Description: Kernel heap allocation manager implementation.
 */

#include "KernelHeapManager.hpp"

#include <stddef.h>

namespace
{
constexpr uint32_t ALLOCATION_MAGIC     = 0x4B484D47;
constexpr uint16_t LARGE_CLASS_SENTINEL = 0xFFFF;
constexpr int32_t  INVALID_LIST_INDEX   = -1;

constexpr size_t SLAB_CLASS_SIZES[7] = {
        64, 128, 256, 512, 1024, 2048, 4096,
};
} // namespace

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

    ManagedHeapSize = GetManagedHeapSize();
    SlabCount       = GetEffectiveSlabCount();
    ResetAllocatorState();
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
 * Function: KernelHeapManager::GetEffectiveSlabCount
 * Description: Returns the number of full slab pages available for allocations.
 * Parameters:
 *   None
 * Returns:
 *   size_t - Usable slab count.
 */
size_t KernelHeapManager::GetEffectiveSlabCount() const
{
    return GetManagedHeapSize() / static_cast<size_t>(SLAB_PAGE_SIZE);
}

/**
 * Function: KernelHeapManager::SelectClassIndex
 * Description: Selects slab class index for requested payload size plus allocation header.
 * Parameters:
 *   size_t Size - Requested user payload size.
 * Returns:
 *   size_t - Matching class index, or SLAB_CLASS_COUNT when request must use large allocation path.
 */
size_t KernelHeapManager::SelectClassIndex(size_t Size) const
{
    if (Size > (SIZE_MAX - sizeof(AllocationHeader)))
    {
        return SLAB_CLASS_COUNT;
    }

    size_t RequiredBytes = Size + sizeof(AllocationHeader);
    for (size_t ClassIndex = 0; ClassIndex < SLAB_CLASS_COUNT; ++ClassIndex)
    {
        if (SLAB_CLASS_SIZES[ClassIndex] >= RequiredBytes)
        {
            return ClassIndex;
        }
    }

    return SLAB_CLASS_COUNT;
}

/**
 * Function: KernelHeapManager::GetClassObjectSize
 * Description: Returns object size in bytes for a slab class.
 * Parameters:
 *   size_t ClassIndex - Slab class index.
 * Returns:
 *   size_t - Object size for that class.
 */
size_t KernelHeapManager::GetClassObjectSize(size_t ClassIndex) const
{
    if (ClassIndex >= SLAB_CLASS_COUNT)
    {
        return 0;
    }

    return SLAB_CLASS_SIZES[ClassIndex];
}

/**
 * Function: KernelHeapManager::GetSlabBaseAddress
 * Description: Returns virtual base address for a slab page index.
 * Parameters:
 *   size_t SlabIndex - Slab page index.
 * Returns:
 *   uintptr_t - Base address of slab page.
 */
uintptr_t KernelHeapManager::GetSlabBaseAddress(size_t SlabIndex) const
{
    return static_cast<uintptr_t>(HeapStart) + (SlabIndex * static_cast<size_t>(SLAB_PAGE_SIZE));
}

/**
 * Function: KernelHeapManager::ResetAllocatorState
 * Description: Resets slab descriptors and per-class available slab lists.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void KernelHeapManager::ResetAllocatorState()
{
    for (size_t ClassIndex = 0; ClassIndex < SLAB_CLASS_COUNT; ++ClassIndex)
    {
        ClassAvailableHead[ClassIndex] = INVALID_LIST_INDEX;
    }

    for (size_t SlabIndex = 0; SlabIndex < MAX_SLAB_COUNT; ++SlabIndex)
    {
        Slabs[SlabIndex].State         = SlabFree;
        Slabs[SlabIndex].ClassIndex    = 0;
        Slabs[SlabIndex].Reserved      = 0;
        Slabs[SlabIndex].OwnerOrSpan   = 0;
        Slabs[SlabIndex].TotalObjects  = 0;
        Slabs[SlabIndex].FreeObjects   = 0;
        Slabs[SlabIndex].FreeList      = nullptr;
        Slabs[SlabIndex].PrevAvailable = INVALID_LIST_INDEX;
        Slabs[SlabIndex].NextAvailable = INVALID_LIST_INDEX;
    }
}

/**
 * Function: KernelHeapManager::LinkSlabIntoClassList
 * Description: Adds a slab to the list of slabs with free objects for a specific class.
 * Parameters:
 *   size_t SlabIndex - Slab index to link.
 *   size_t ClassIndex - Slab class index.
 * Returns:
 *   void - No return value.
 */
void KernelHeapManager::LinkSlabIntoClassList(size_t SlabIndex, size_t ClassIndex)
{
    SlabDescriptor& Descriptor = Slabs[SlabIndex];
    if (Descriptor.PrevAvailable != INVALID_LIST_INDEX || Descriptor.NextAvailable != INVALID_LIST_INDEX || ClassAvailableHead[ClassIndex] == static_cast<int32_t>(SlabIndex))
    {
        return;
    }

    int32_t HeadIndex        = ClassAvailableHead[ClassIndex];
    Descriptor.PrevAvailable = INVALID_LIST_INDEX;
    Descriptor.NextAvailable = HeadIndex;
    if (HeadIndex != INVALID_LIST_INDEX)
    {
        Slabs[static_cast<size_t>(HeadIndex)].PrevAvailable = static_cast<int32_t>(SlabIndex);
    }

    ClassAvailableHead[ClassIndex] = static_cast<int32_t>(SlabIndex);
}

/**
 * Function: KernelHeapManager::UnlinkSlabFromClassList
 * Description: Removes a slab from the list of slabs with free objects for a specific class.
 * Parameters:
 *   size_t SlabIndex - Slab index to unlink.
 *   size_t ClassIndex - Slab class index.
 * Returns:
 *   void - No return value.
 */
void KernelHeapManager::UnlinkSlabFromClassList(size_t SlabIndex, size_t ClassIndex)
{
    SlabDescriptor& Descriptor = Slabs[SlabIndex];
    int32_t         PrevIndex  = Descriptor.PrevAvailable;
    int32_t         NextIndex  = Descriptor.NextAvailable;

    if (PrevIndex != INVALID_LIST_INDEX)
    {
        Slabs[static_cast<size_t>(PrevIndex)].NextAvailable = NextIndex;
    }
    else if (ClassAvailableHead[ClassIndex] == static_cast<int32_t>(SlabIndex))
    {
        ClassAvailableHead[ClassIndex] = NextIndex;
    }

    if (NextIndex != INVALID_LIST_INDEX)
    {
        Slabs[static_cast<size_t>(NextIndex)].PrevAvailable = PrevIndex;
    }

    Descriptor.PrevAvailable = INVALID_LIST_INDEX;
    Descriptor.NextAvailable = INVALID_LIST_INDEX;
}

/**
 * Function: KernelHeapManager::InitializeSmallSlab
 * Description: Initializes a free slab page for a specific fixed-size object class.
 * Parameters:
 *   size_t SlabIndex - Slab index to initialize.
 *   size_t ClassIndex - Slab class index.
 * Returns:
 *   bool - True on success, false on failure.
 */
bool KernelHeapManager::InitializeSmallSlab(size_t SlabIndex, size_t ClassIndex)
{
    if (SlabIndex >= SlabCount || ClassIndex >= SLAB_CLASS_COUNT)
    {
        return false;
    }

    size_t ObjectSize = GetClassObjectSize(ClassIndex);
    if (ObjectSize == 0)
    {
        return false;
    }

    size_t ObjectsPerSlab = static_cast<size_t>(SLAB_PAGE_SIZE) / ObjectSize;
    if (ObjectsPerSlab == 0)
    {
        return false;
    }

    SlabDescriptor& Descriptor = Slabs[SlabIndex];
    Descriptor.State           = SlabSmall;
    Descriptor.ClassIndex      = static_cast<uint8_t>(ClassIndex);
    Descriptor.Reserved        = 0;
    Descriptor.OwnerOrSpan     = 1;
    Descriptor.TotalObjects    = static_cast<uint32_t>(ObjectsPerSlab);
    Descriptor.FreeObjects     = static_cast<uint32_t>(ObjectsPerSlab);
    Descriptor.FreeList        = nullptr;
    Descriptor.PrevAvailable   = INVALID_LIST_INDEX;
    Descriptor.NextAvailable   = INVALID_LIST_INDEX;

    uintptr_t SlabBaseAddress = GetSlabBaseAddress(SlabIndex);
    for (size_t ObjectIndex = 0; ObjectIndex < ObjectsPerSlab; ++ObjectIndex)
    {
        uintptr_t   ObjectAddress = SlabBaseAddress + (ObjectIndex * ObjectSize);
        FreeObject* Node          = reinterpret_cast<FreeObject*>(ObjectAddress);
        Node->Next                = Descriptor.FreeList;
        Descriptor.FreeList       = Node;
    }

    LinkSlabIntoClassList(SlabIndex, ClassIndex);
    return true;
}

/**
 * Function: KernelHeapManager::FindFreeSlabRun
 * Description: Finds a contiguous run of free slab pages.
 * Parameters:
 *   size_t SlabSpan - Number of contiguous slab pages required.
 * Returns:
 *   int64_t - Start slab index, or -1 if unavailable.
 */
int64_t KernelHeapManager::FindFreeSlabRun(size_t SlabSpan) const
{
    if (SlabSpan == 0 || SlabSpan > SlabCount)
    {
        return -1;
    }

    size_t RunStart  = 0;
    size_t RunLength = 0;
    for (size_t SlabIndex = 0; SlabIndex < SlabCount; ++SlabIndex)
    {
        if (Slabs[SlabIndex].State != SlabFree)
        {
            RunLength = 0;
            continue;
        }

        if (RunLength == 0)
        {
            RunStart = SlabIndex;
        }

        ++RunLength;
        if (RunLength == SlabSpan)
        {
            return static_cast<int64_t>(RunStart);
        }
    }

    return -1;
}

/**
 * Function: KernelHeapManager::ReleaseSlabRun
 * Description: Returns a contiguous slab run to the free pool.
 * Parameters:
 *   size_t StartSlab - First slab index of the run.
 *   size_t SlabSpan - Number of slabs in the run.
 * Returns:
 *   void - No return value.
 */
void KernelHeapManager::ReleaseSlabRun(size_t StartSlab, size_t SlabSpan)
{
    if (SlabSpan == 0 || StartSlab + SlabSpan > SlabCount)
    {
        return;
    }

    for (size_t Offset = 0; Offset < SlabSpan; ++Offset)
    {
        SlabDescriptor& Descriptor = Slabs[StartSlab + Offset];
        Descriptor.State           = SlabFree;
        Descriptor.ClassIndex      = 0;
        Descriptor.Reserved        = 0;
        Descriptor.OwnerOrSpan     = 0;
        Descriptor.TotalObjects    = 0;
        Descriptor.FreeObjects     = 0;
        Descriptor.FreeList        = nullptr;
        Descriptor.PrevAvailable   = INVALID_LIST_INDEX;
        Descriptor.NextAvailable   = INVALID_LIST_INDEX;
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

    if (SlabCount == 0)
    {
        return nullptr;
    }

    size_t ClassIndex = SelectClassIndex(Size);

    if (ClassIndex < SLAB_CLASS_COUNT)
    {
        int32_t AvailableSlabIndex = ClassAvailableHead[ClassIndex];
        if (AvailableSlabIndex == INVALID_LIST_INDEX)
        {
            int64_t NewSlabIndex = FindFreeSlabRun(1);
            if (NewSlabIndex < 0)
            {
                return nullptr;
            }

            if (!InitializeSmallSlab(static_cast<size_t>(NewSlabIndex), ClassIndex))
            {
                return nullptr;
            }

            AvailableSlabIndex = ClassAvailableHead[ClassIndex];
            if (AvailableSlabIndex == INVALID_LIST_INDEX)
            {
                return nullptr;
            }
        }

        SlabDescriptor& Descriptor = Slabs[static_cast<size_t>(AvailableSlabIndex)];
        if (Descriptor.FreeList == nullptr || Descriptor.FreeObjects == 0)
        {
            return nullptr;
        }

        FreeObject* ObjectNode = Descriptor.FreeList;
        Descriptor.FreeList    = ObjectNode->Next;
        --Descriptor.FreeObjects;

        if (Descriptor.FreeObjects == 0)
        {
            UnlinkSlabFromClassList(static_cast<size_t>(AvailableSlabIndex), ClassIndex);
        }

        AllocationHeader* Header = reinterpret_cast<AllocationHeader*>(ObjectNode);
        Header->Magic            = ALLOCATION_MAGIC;
        Header->SlabIndex        = static_cast<uint32_t>(AvailableSlabIndex);
        Header->ClassIndex       = static_cast<uint16_t>(ClassIndex);
        Header->Reserved         = 0;

        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(Header) + sizeof(AllocationHeader));
    }

    if (Size > (SIZE_MAX - sizeof(AllocationHeader)))
    {
        return nullptr;
    }

    size_t TotalBytesNeeded = Size + sizeof(AllocationHeader);
    size_t SlabSpanNeeded   = (TotalBytesNeeded + (static_cast<size_t>(SLAB_PAGE_SIZE) - 1)) / static_cast<size_t>(SLAB_PAGE_SIZE);
    if (SlabSpanNeeded == 0)
    {
        return nullptr;
    }

    int64_t StartSlab = FindFreeSlabRun(SlabSpanNeeded);
    if (StartSlab < 0)
    {
        return nullptr;
    }

    size_t LargeSlabIndex               = static_cast<size_t>(StartSlab);
    Slabs[LargeSlabIndex].State         = SlabLargeHead;
    Slabs[LargeSlabIndex].ClassIndex    = 0;
    Slabs[LargeSlabIndex].Reserved      = 0;
    Slabs[LargeSlabIndex].OwnerOrSpan   = static_cast<uint32_t>(SlabSpanNeeded);
    Slabs[LargeSlabIndex].TotalObjects  = 1;
    Slabs[LargeSlabIndex].FreeObjects   = 0;
    Slabs[LargeSlabIndex].FreeList      = nullptr;
    Slabs[LargeSlabIndex].PrevAvailable = INVALID_LIST_INDEX;
    Slabs[LargeSlabIndex].NextAvailable = INVALID_LIST_INDEX;

    for (size_t Offset = 1; Offset < SlabSpanNeeded; ++Offset)
    {
        SlabDescriptor& TailDescriptor = Slabs[LargeSlabIndex + Offset];
        TailDescriptor.State           = SlabLargeTail;
        TailDescriptor.ClassIndex      = 0;
        TailDescriptor.Reserved        = 0;
        TailDescriptor.OwnerOrSpan     = static_cast<uint32_t>(LargeSlabIndex);
        TailDescriptor.TotalObjects    = 0;
        TailDescriptor.FreeObjects     = 0;
        TailDescriptor.FreeList        = nullptr;
        TailDescriptor.PrevAvailable   = INVALID_LIST_INDEX;
        TailDescriptor.NextAvailable   = INVALID_LIST_INDEX;
    }

    uintptr_t         HeaderAddress = GetSlabBaseAddress(LargeSlabIndex);
    AllocationHeader* Header        = reinterpret_cast<AllocationHeader*>(HeaderAddress);
    Header->Magic                   = ALLOCATION_MAGIC;
    Header->SlabIndex               = static_cast<uint32_t>(LargeSlabIndex);
    Header->ClassIndex              = LARGE_CLASS_SENTINEL;
    Header->Reserved                = 0;

    return reinterpret_cast<void*>(HeaderAddress + sizeof(AllocationHeader));
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
    if (ManagedHeapSize == 0 || SlabCount == 0)
    {
        return;
    }

    uintptr_t HeapBase  = static_cast<uintptr_t>(HeapStart);
    uintptr_t HeapLimit = HeapBase + (SlabCount * static_cast<size_t>(SLAB_PAGE_SIZE));
    if (UserAddress < (HeapBase + sizeof(AllocationHeader)) || UserAddress >= HeapLimit)
    {
        return;
    }

    uintptr_t HeaderAddress = UserAddress - sizeof(AllocationHeader);
    if (HeaderAddress < HeapBase || HeaderAddress >= HeapLimit)
    {
        return;
    }

    AllocationHeader* Header = reinterpret_cast<AllocationHeader*>(HeaderAddress);
    if (Header->Magic != ALLOCATION_MAGIC)
    {
        return;
    }

    size_t SlabIndex = static_cast<size_t>(Header->SlabIndex);
    if (SlabIndex >= SlabCount)
    {
        return;
    }

    if (Header->ClassIndex == LARGE_CLASS_SENTINEL)
    {
        SlabDescriptor& HeadDescriptor = Slabs[SlabIndex];
        if (HeadDescriptor.State != SlabLargeHead)
        {
            return;
        }

        size_t SlabSpan = static_cast<size_t>(HeadDescriptor.OwnerOrSpan);
        if (SlabSpan == 0 || SlabIndex + SlabSpan > SlabCount)
        {
            return;
        }

        if (HeaderAddress != GetSlabBaseAddress(SlabIndex))
        {
            return;
        }

        Header->Magic = 0;
        ReleaseSlabRun(SlabIndex, SlabSpan);
        return;
    }

    size_t ClassIndex = static_cast<size_t>(Header->ClassIndex);
    if (ClassIndex >= SLAB_CLASS_COUNT)
    {
        return;
    }

    SlabDescriptor& Descriptor = Slabs[SlabIndex];
    if (Descriptor.State != SlabSmall || Descriptor.ClassIndex != static_cast<uint8_t>(ClassIndex))
    {
        return;
    }

    uintptr_t SlabBaseAddress = GetSlabBaseAddress(SlabIndex);
    size_t    ObjectSize      = GetClassObjectSize(ClassIndex);
    if (ObjectSize == 0)
    {
        return;
    }

    if (HeaderAddress < SlabBaseAddress || HeaderAddress >= (SlabBaseAddress + static_cast<size_t>(SLAB_PAGE_SIZE)))
    {
        return;
    }

    size_t OffsetInSlab = static_cast<size_t>(HeaderAddress - SlabBaseAddress);
    if ((OffsetInSlab % ObjectSize) != 0)
    {
        return;
    }

    if (Descriptor.FreeObjects >= Descriptor.TotalObjects)
    {
        return;
    }

    Header->Magic = 0;

    if (Descriptor.FreeObjects == 0)
    {
        LinkSlabIntoClassList(SlabIndex, ClassIndex);
    }

    FreeObject* ObjectNode = reinterpret_cast<FreeObject*>(HeaderAddress);
    ObjectNode->Next       = Descriptor.FreeList;
    Descriptor.FreeList    = ObjectNode;
    ++Descriptor.FreeObjects;

    if (Descriptor.FreeObjects == Descriptor.TotalObjects)
    {
        UnlinkSlabFromClassList(SlabIndex, ClassIndex);
        ReleaseSlabRun(SlabIndex, 1);
    }
}
