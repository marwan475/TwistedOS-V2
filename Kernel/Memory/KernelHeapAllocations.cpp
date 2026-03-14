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

[[noreturn]] void KernelAllocationFailure()
{
    while (1)
    {
        __asm__ __volatile__("cli; hlt");
    }
}

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
}

void KernelSetAllocator(KernelAllocFn AllocFn, KernelFreeFn FreeFn)
{
    ActiveAlloc = AllocFn;
    ActiveFree  = FreeFn;
}

void KernelUseDispatcherAllocator()
{
    KernelSetAllocator(&KernelAllocFromDispatcher, &KernelFreeFromDispatcher);
}

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

void* operator new(size_t Size, const std::nothrow_t&) noexcept
{
    if (ActiveAlloc == nullptr)
    {
        return nullptr;
    }

    return ActiveAlloc(Size);
}

void* operator new[](size_t Size, const std::nothrow_t&) noexcept
{
    if (ActiveAlloc == nullptr)
    {
        return nullptr;
    }

    return ActiveAlloc(Size);
}

void operator delete(void* Ptr) noexcept
{
    if (Ptr == nullptr || ActiveFree == nullptr)
    {
        return;
    }

    ActiveFree(Ptr);
}

void operator delete[](void* Ptr) noexcept
{
    if (Ptr == nullptr || ActiveFree == nullptr)
    {
        return;
    }

    ActiveFree(Ptr);
}

void operator delete(void* Ptr, size_t) noexcept
{
    if (Ptr == nullptr || ActiveFree == nullptr)
    {
        return;
    }

    ActiveFree(Ptr);
}

void operator delete[](void* Ptr, size_t) noexcept
{
    if (Ptr == nullptr || ActiveFree == nullptr)
    {
        return;
    }

    ActiveFree(Ptr);
}