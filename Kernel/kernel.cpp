/**
 * File: kernel.cpp
 * Author: Marwan Mostafa
 * Description: Main kernel entry and initialization flow.
 */

#include "../utils/KernelParameters.hpp"
#include "Layers/Dispatcher.hpp"
#include "Layers/Resource/ResourceLayer.hpp"
#include "Layers/Resource/TTY.hpp"
#include "Testing/KernelSelfTests.hpp"

#include <Arch/x86.hpp>
#include <CommonUtils.hpp>
#include <Logging/FrameBufferConsole.hpp>
#include <Memory/PhysicalMemoryManager.hpp>
#include <Memory/VirtualMemoryManager.hpp>
#include <stdint.h>

extern "C" void DispatcherEntry(DispatcherParameters Params);

extern "C" void EFIAPI KernelEntry(KernelParameters KernelArgs) __attribute__((section(".text.entry")));

static Dispatcher KernelDispatcher;

// Uefi sets us up in 64bit long mode with identity mapped pages
extern "C"
{
    /**
     * Function: KernelEntry
     * Description: Initializes core x86 CPU requirements, sets up memory managers, maps the kernel heap into the higher half, then enters the dispatcher.
     * Parameters:
     *   KernelParameters KernelArgs - Boot parameters provided by the bootloader, including graphics mode, memory map, paging, and initramfs metadata.
     * Returns:
     *   void - No value is returned.
     */
    void EFIAPI KernelEntry(KernelParameters KernelArgs)
    {
        FrameBufferConsole Console;
        Console.Initialize((uint32_t*) KernelArgs.GopMode.FrameBufferBase, KernelArgs.GopMode.Info->HorizontalResolution, KernelArgs.GopMode.Info->VerticalResolution,
                           KernelArgs.GopMode.Info->PixelsPerScanLine);
        Console.Clear();
        FrameBufferConsole::SetActive(&Console);
        Console.printf_("Framebuffer console Initialized\n");

        Console.printf_("Kernel Loaded at %p to %p\n", KERNEL_BASE_VIRTUAL_ADDR, KernelArgs.KernelEndVirtual);
        Console.printf_("Initramfs loaded at %p (%llu bytes)\n", (void*) KernelArgs.InitramfsAddress, (unsigned long long) KernelArgs.InitramfsSize);

        // Initialize GDT and TSS
        InitGDT();
        Console.printf_("GDT/TSS Initialized\n");

        // Initialize Interrupts
        InitInterrupts();
        Console.printf_("Interrupts Initialized\n");

        // Set up MSRs for syscall/sysret
        InitSystemCalls();
        Console.printf_("System calls Initialized\n");

        InitTimer();
        Console.printf_("Timer Initialized\n");

        PhysicalMemoryManager PMM(KernelArgs.MemoryMap, KernelArgs.NextPageAddress, KernelArgs.CurrentDescriptor, KernelArgs.RemainingPagesInDescriptor);

        Console.printf_("Physical Memory Manager Initialized\n");

        UINTN TotalUsableMemoryBytes = PMM.TotalUsableMemoryBytes();
        UINTN TotalUsableMemoryMiB   = TotalUsableMemoryBytes / (1024 * 1024);
        UINTN TotalUsableMemoryGiB   = TotalUsableMemoryMiB / 1024;
        Console.printf_("Total usable memory: %llu bytes (%llu MiB / %llu GiB)\n", (unsigned long long) TotalUsableMemoryBytes, (unsigned long long) TotalUsableMemoryMiB,
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

        UINTN KernelHeapVirtualAddrEnd = VMM.MapRange((UINTN) KernelHeapPhysicalAddr, KernelHeapVirtualAddrStart, KERNEL_HEAP_PAGES, PageMappingFlags(false, true));
        Console.printf_("Kernel heap mapped to virtual address range: %p - %p\n", KernelHeapVirtualAddrStart, KernelHeapVirtualAddrEnd);

        kmemset((void*) KernelHeapVirtualAddrStart, 0, KERNEL_HEAP_PAGES * PAGE_SIZE);

        DispatcherParameters Params
                = {&PMM, &VMM, &Console, (uint64_t) KernelHeapVirtualAddrStart, (uint64_t) KernelHeapVirtualAddrEnd, KernelArgs.GopMode, KernelArgs.InitramfsAddress, KernelArgs.InitramfsSize};
        DispatcherEntry(Params);
    }

    /**
     * Function: DispatcherEntry
     * Description: Activates the global dispatcher, initializes layers, starts self-tests, and enables scheduling.
     * Parameters:
     *   DispatcherParameters Params - Runtime parameters and subsystem pointers passed to dispatcher initialization.
     * Returns:
     *   void - No value is returned.
     */
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

        ActiveDispatcher->GetLogicLayer()->CreateNullProcess();

        ActiveDispatcher->GetLogicLayer()->GetVirtualFileSystem()->MountInitRamFileSystem(ActiveDispatcher->GetResourceLayer()->GetRamFileSystemManager());

        TTY*         Terminal = ActiveDispatcher->GetResourceLayer()->GetTTY();
        FrameBuffer* FB       = ActiveDispatcher->GetResourceLayer()->GetFrameBuffer();
        if (Terminal != nullptr)
        {
            ActiveDispatcher->GetLogicLayer()->GetVirtualFileSystem()->RegisterDevice("/dev/tty", Terminal, Terminal->GetFileOperations());
        }

        if (FB != nullptr)
        {
            ActiveDispatcher->GetLogicLayer()->GetVirtualFileSystem()->RegisterDevice("/dev/fb0", FB, FB->GetFileOperations());
        }

        ActiveDispatcher->GetLogicLayer()->RegisterPartitionDevices();

        if (!ActiveDispatcher->GetLogicLayer()->InitializeRootFileSystem("/dev/sda2"))
        {
            if (Terminal != nullptr)
            {
                Terminal->printf_("kernel startup failed: unable to initialize root filesystem on /dev/sda2\n");
            }

            while (1)
                __asm__ __volatile__("hlt");
        }

        ActiveDispatcher->GetLogicLayer()->GetVirtualFileSystem()->PrintVFS(ActiveDispatcher->GetResourceLayer()->GetTTY());

#ifdef STEST_BUILD
        if (!KernelSelfTestStart(ActiveDispatcher))
        {
            ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("KernelSelfTestStart setup failed\n");
            while (1)
                __asm__ __volatile__("hlt");
        }
#else
        ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("Kernel self-tests disabled (set STEST=1 to enable)\n");
#endif

        ActiveDispatcher->GetLogicLayer()->CreateUserProcessFromVFS("/init");

        ActiveDispatcher->GetLogicLayer()->EnableScheduling();

        while (1)
            __asm__ __volatile__("hlt");
    }
}
