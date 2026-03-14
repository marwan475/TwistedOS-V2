
#include "../utils/KernelParameters.hpp"
#include "Layers/Dispatcher.hpp"
#include "Layers/Resource/ResourceLayer.hpp"

#include <Arch/x86.hpp>
#include <CommonUtils.hpp>
#include <Logging/FrameBufferConsole.hpp>
#include <Memory/PhysicalMemoryManager.hpp>
#include <Memory/VirtualMemoryManager.hpp>
#include <stdint.h>

#define KERNEL_HEAP_PAGES 16
#define KERNEL_HEAP_START 0xFFFFFFFF82000000
#define KERNEL_BASE 0xFFFFFFFF80000000

extern "C" void DispatcherEntry(DispatcherParameters Params);

extern "C" void EFIAPI KernelEntry(KernelParameters KernelArgs) __attribute__((section(".text.entry")));

static Dispatcher KernelDispatcher;

// Uefi sets us up in 64bit long mode with identity mapped pages
extern "C"
{
    void EFIAPI KernelEntry(KernelParameters KernelArgs)
    {
        FrameBufferConsole Console;
        Console.Initialize((uint32_t*) KernelArgs.GopMode.FrameBufferBase,
                           KernelArgs.GopMode.Info->HorizontalResolution, KernelArgs.GopMode.Info->VerticalResolution,
                           KernelArgs.GopMode.Info->PixelsPerScanLine);
        Console.Clear();
        FrameBufferConsole::SetActive(&Console);
        Console.printf_("Framebuffer console Initialized\n");

        Console.printf_("Kernel Loaded at %p to %p\n", KERNEL_BASE, KernelArgs.KernelEndVirtual);

        // Initialize GDT and TSS
        InitGDT();
        Console.printf_("GDT/TSS Initialized\n");

        // Initialize Interrupts
        InitInterrupts();
        Console.printf_("Interrupts Initialized\n");

        PhysicalMemoryManager PMM(KernelArgs.MemoryMap, KernelArgs.NextPageAddress, KernelArgs.CurrentDescriptor,
                                  KernelArgs.RemainingPagesInDescriptor);

        Console.printf_("Physical Memory Manager Initialized\n");

        UINTN TotalUsableMemoryBytes = PMM.TotalUsableMemoryBytes();
        UINTN TotalUsableMemoryMiB   = TotalUsableMemoryBytes / (1024 * 1024);
        UINTN TotalUsableMemoryGiB   = TotalUsableMemoryMiB / 1024;
        Console.printf_("Total usable memory: %llu bytes (%llu MiB / %llu GiB)\n",
                        (unsigned long long) TotalUsableMemoryBytes, (unsigned long long) TotalUsableMemoryMiB,
                        (unsigned long long) TotalUsableMemoryGiB);

        UINTN TotalPages = PMM.TotalPages();
        Console.printf_("Total number of pages: %llu\n", (unsigned long long) TotalPages);

        PMM.InitializeMemoryDescriptors();
        Console.printf_("Memory Descriptor initialized\n");

        VirtualMemoryManager VMM(KernelArgs.PageMapL4Table, PMM);
        Console.printf_("Virtual Memory Manager Initialized\n");

        void* KernelHeapPhysicalAddr = PMM.AllocatePagesFromDescriptor(KERNEL_HEAP_PAGES);
        if (KernelHeapPhysicalAddr == NULL)
        {
            Console.printf_("Failed to allocate kernel heap\n");
        }
        else
        {
            Console.printf_("Kernel heap allocated at physical address: %p\n", KernelHeapPhysicalAddr);
        }

        if (KernelArgs.KernelEndVirtual >= KERNEL_HEAP_START)
        {
            Console.printf_("Error: Kernel end virtual address overlaps with kernel heap start address\n");
            while (1)
                __asm__ __volatile__("hlt");
        }

        UINTN KernelHeapVirtualAddrStart = KERNEL_HEAP_START;

        UINTN KernelHeapVirtualAddrEnd
                = VMM.MapRange((UINTN) KernelHeapPhysicalAddr, KernelHeapVirtualAddrStart, KERNEL_HEAP_PAGES);
        Console.printf_("Kernel heap mapped to virtual address range: %p - %p\n", KernelHeapVirtualAddrStart,
                        KernelHeapVirtualAddrEnd);

        kmemset((void*) KernelHeapVirtualAddrStart, 0, KERNEL_HEAP_PAGES * PAGE_SIZE);

        DispatcherParameters Params
                = {&PMM, &VMM, &Console, (uint64_t) KernelHeapVirtualAddrStart, (uint64_t) KernelHeapVirtualAddrEnd};
        DispatcherEntry(Params);
    }

    void DispatcherEntry(DispatcherParameters Params)
    {
        Dispatcher::SetActive(&KernelDispatcher);

        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher == nullptr)
        {
            while (1)
                __asm__ __volatile__("hlt");
        }

        Params.Console->printf_("Entered Dispatcher\n");
        Params.Console->printf_("Initializing layers\n");

        ActiveDispatcher->InitializeLayers(Params);

        while (1)
            __asm__ __volatile__("hlt");
    }
}
