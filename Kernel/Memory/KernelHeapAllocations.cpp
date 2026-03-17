#include "KernelHeapAllocations.hpp"

#include "Layers/Dispatcher.hpp"

#include <new>

extern "C"
{
    void* __dso_handle = nullptr;

    int __cxa_atexit(void (*)(void*), void*, void*)
    {
        return 0;
    }
}

namespace
{
KernelAllocFn ActiveAlloc = nullptr;
KernelFreeFn  ActiveFree  = nullptr;

/**
 * Function: KernelAllocationFailure
 * Description: Halts the CPU indefinitely when a kernel allocation fails.
 * Parameters:
 *   None
 * Returns:
 *   void - This function never returns.
 */
[[noreturn]] void KernelAllocationFailure()
{
    while (1)
    {
        __asm__ __volatile__("cli; hlt");
    }
}

/**
 * Function: KernelAllocFromDispatcher
 * Description: Allocates kernel heap memory through the active dispatcher's resource layer.
 * Parameters:
 *   size_t Size - The number of bytes to allocate.
 * Returns:
 *   void* - Pointer to the allocated memory, or nullptr if allocation cannot be performed.
 */
void* KernelAllocFromDispatcher(size_t Size)
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        return nullptr;
    }

    ResourceLayer* Resource = ActiveDispatcher->GetResourceLayer();
    if (Resource == nullptr)
    {
        return nullptr;
    }

    return Resource->kmalloc(Size);
}

/**
 * Function: KernelFreeFromDispatcher
 * Description: Frees kernel heap memory through the active dispatcher's resource layer.
 * Parameters:
 *   void* Ptr - Pointer to the memory block to free.
 * Returns:
 *   void - No return value.
 */
void KernelFreeFromDispatcher(void* Ptr)
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        return;
    }

    ResourceLayer* Resource = ActiveDispatcher->GetResourceLayer();
    if (Resource == nullptr)
    {
        return;
    }

    Resource->kfree(Ptr);
}
} // namespace

/**
 * Function: KernelSetAllocator
 * Description: Sets the active kernel allocation and free function pointers.
 * Parameters:
 *   KernelAllocFn AllocFn - Allocation function to use for kernel allocations.
 *   KernelFreeFn FreeFn - Free function to use for kernel deallocations.
 * Returns:
 *   void - No return value.
 */
void KernelSetAllocator(KernelAllocFn AllocFn, KernelFreeFn FreeFn)
{
    ActiveAlloc = AllocFn;
    ActiveFree  = FreeFn;
}

/**
 * Function: KernelUseDispatcherAllocator
 * Description: Configures kernel allocations to use the dispatcher's resource layer allocator.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void KernelUseDispatcherAllocator()
{
    KernelSetAllocator(&KernelAllocFromDispatcher, &KernelFreeFromDispatcher);
}

/**
 * Function: operator new
 * Description: Allocates memory for a single object using the active kernel allocator.
 * Parameters:
 *   size_t Size - The number of bytes to allocate.
 * Returns:
 *   void* - Pointer to the allocated memory block.
 */
void* operator new(size_t Size)
{
    if (ActiveAlloc == nullptr)
    {
        KernelAllocationFailure();
    }

    void* Ptr = ActiveAlloc(Size);
    if (Ptr == nullptr)
    {
        KernelAllocationFailure();
    }

    return Ptr;
}

/**
 * Function: operator new[]
 * Description: Allocates memory for an array object using the active kernel allocator.
 * Parameters:
 *   size_t Size - The number of bytes to allocate.
 * Returns:
 *   void* - Pointer to the allocated memory block.
 */
void* operator new[](size_t Size)
{
    if (ActiveAlloc == nullptr)
    {
        KernelAllocationFailure();
    }

    void* Ptr = ActiveAlloc(Size);
    if (Ptr == nullptr)
    {
        KernelAllocationFailure();
    }

    return Ptr;
}

/**
 * Function: operator new (nothrow)
 * Description: Allocates memory for a single object and returns nullptr instead of halting on failure.
 * Parameters:
 *   size_t Size - The number of bytes to allocate.
 *   const std::nothrow_t& - Nothrow allocation tag.
 * Returns:
 *   void* - Pointer to allocated memory, or nullptr on failure.
 */
void* operator new(size_t Size, const std::nothrow_t&) noexcept
{
    if (ActiveAlloc == nullptr)
    {
        return nullptr;
    }

    return ActiveAlloc(Size);
}

/**
 * Function: operator new[] (nothrow)
 * Description: Allocates memory for an array and returns nullptr instead of halting on failure.
 * Parameters:
 *   size_t Size - The number of bytes to allocate.
 *   const std::nothrow_t& - Nothrow allocation tag.
 * Returns:
 *   void* - Pointer to allocated memory, or nullptr on failure.
 */
void* operator new[](size_t Size, const std::nothrow_t&) noexcept
{
    if (ActiveAlloc == nullptr)
    {
        return nullptr;
    }

    return ActiveAlloc(Size);
}

/**
 * Function: operator delete
 * Description: Frees memory for a single object using the active kernel free function.
 * Parameters:
 *   void* Ptr - Pointer to the memory block to free.
 * Returns:
 *   void - No return value.
 */
void operator delete(void* Ptr) noexcept
{
    if (Ptr == nullptr || ActiveFree == nullptr)
    {
        return;
    }

    ActiveFree(Ptr);
}

/**
 * Function: operator delete[]
 * Description: Frees memory for an array object using the active kernel free function.
 * Parameters:
 *   void* Ptr - Pointer to the memory block to free.
 * Returns:
 *   void - No return value.
 */
void operator delete[](void* Ptr) noexcept
{
    if (Ptr == nullptr || ActiveFree == nullptr)
    {
        return;
    }

    ActiveFree(Ptr);
}

/**
 * Function: operator delete (sized)
 * Description: Sized delete overload that frees memory for a single object.
 * Parameters:
 *   void* Ptr - Pointer to the memory block to free.
 *   size_t - Size of the allocation (unused).
 * Returns:
 *   void - No return value.
 */
void operator delete(void* Ptr, size_t) noexcept
{
    if (Ptr == nullptr || ActiveFree == nullptr)
    {
        return;
    }

    ActiveFree(Ptr);
}

/**
 * Function: operator delete[] (sized)
 * Description: Sized delete overload that frees memory for an array allocation.
 * Parameters:
 *   void* Ptr - Pointer to the memory block to free.
 *   size_t - Size of the allocation (unused).
 * Returns:
 *   void - No return value.
 */
void operator delete[](void* Ptr, size_t) noexcept
{
    if (Ptr == nullptr || ActiveFree == nullptr)
    {
        return;
    }

    ActiveFree(Ptr);
}