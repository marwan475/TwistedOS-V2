
#include "../utils/KernelParameters.hpp"
#include "Layers/Dispatcher.hpp"
#include "Layers/Resource/ResourceLayer.hpp"

#include <Arch/x86.hpp>
#include <CommonUtils.hpp>
#include <Logging/FrameBufferConsole.hpp>
#include <Memory/PhysicalMemoryManager.hpp>
#include <Memory/VirtualMemoryManager.hpp>
#include <stdint.h>

#define KERNEL_HEAP_PAGES 32
#define KERNEL_HEAP_START 0xFFFFFFFF82000000
#define KERNEL_BASE 0xFFFFFFFF80000000

#define SCHEDULE_INTERVAL_TICKS 100

#define TASK_A_SLEEP_TICKS SCHEDULE_INTERVAL_TICKS
#define TASK_B_SLEEP_TICKS SCHEDULE_INTERVAL_TICKS * 2
#define TASK_C_SLEEP_TICKS SCHEDULE_INTERVAL_TICKS * 3
#define TASK_D_SLEEP_TICKS SCHEDULE_INTERVAL_TICKS * 4

extern "C" void DispatcherEntry(DispatcherParameters Params);

extern "C" void EFIAPI KernelEntry(KernelParameters KernelArgs) __attribute__((section(".text.entry")));

static Dispatcher KernelDispatcher;
static uint8_t    KernelTaskAId = 0xFF;
static uint8_t    KernelTaskBId = 0xFF;
static uint8_t    KernelTaskCId = 0xFF;
static uint8_t    KernelTaskDId = 0xFF;

static void KernelTaskA()
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        while (1)
            __asm__ __volatile__("hlt");
    }

    uint32_t SleepCycle = 0;
    while (1)
    {
        ++SleepCycle;
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[Task A] cycle=%u sleeping for %u ticks\n", SleepCycle, TASK_A_SLEEP_TICKS);
        ActiveDispatcher->GetLogicLayer()->SleepProcess(KernelTaskAId, TASK_A_SLEEP_TICKS);
    }

    while (1)
        __asm__ __volatile__("hlt");
}

static void KernelTaskB()
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        while (1)
            __asm__ __volatile__("hlt");
    }

    uint32_t SleepCycle = 0;
    while (1)
    {
        ++SleepCycle;
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[Task B] cycle=%u sleeping for %u ticks\n", SleepCycle, TASK_B_SLEEP_TICKS);
        ActiveDispatcher->GetLogicLayer()->SleepProcess(KernelTaskBId, TASK_B_SLEEP_TICKS);
    }

    while (1)
        __asm__ __volatile__("hlt");
}

static void KernelTaskC()
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        while (1)
            __asm__ __volatile__("hlt");
    }

    uint32_t SleepCycle = 0;
    while (1)
    {
        ++SleepCycle;
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[Task C] cycle=%u sleeping for %u ticks\n", SleepCycle, TASK_C_SLEEP_TICKS);
        ActiveDispatcher->GetLogicLayer()->SleepProcess(KernelTaskCId, TASK_C_SLEEP_TICKS);
    }

    while (1)
        __asm__ __volatile__("hlt");
}

static void KernelTaskD()
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        while (1)
            __asm__ __volatile__("hlt");
    }

    uint32_t SleepCycle = 0;
    while (1)
    {
        ++SleepCycle;
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[Task D] cycle=%u sleeping for %u ticks\n", SleepCycle, TASK_D_SLEEP_TICKS);
        ActiveDispatcher->GetLogicLayer()->SleepProcess(KernelTaskDId, TASK_D_SLEEP_TICKS);
    }

    while (1)
        __asm__ __volatile__("hlt");
}

// Uefi sets us up in 64bit long mode with identity mapped pages
extern "C"
{
    void EFIAPI KernelEntry(KernelParameters KernelArgs)
    {
        FrameBufferConsole Console;
        Console.Initialize((uint32_t*) KernelArgs.GopMode.FrameBufferBase, KernelArgs.GopMode.Info->HorizontalResolution, KernelArgs.GopMode.Info->VerticalResolution,
                           KernelArgs.GopMode.Info->PixelsPerScanLine);
        Console.Clear();
        FrameBufferConsole::SetActive(&Console);
        Console.printf_("Framebuffer console Initialized\n");

        Console.printf_("Kernel Loaded at %p to %p\n", KERNEL_BASE, KernelArgs.KernelEndVirtual);
        Console.printf_("Initramfs loaded at %p (%llu bytes)\n", (void*) KernelArgs.InitramfsAddress, (unsigned long long) KernelArgs.InitramfsSize);

        // Initialize GDT and TSS
        InitGDT();
        Console.printf_("GDT/TSS Initialized\n");

        // Initialize Interrupts
        InitInterrupts();
        Console.printf_("Interrupts Initialized\n");

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

        UINTN KernelHeapVirtualAddrEnd = VMM.MapRange((UINTN) KernelHeapPhysicalAddr, KernelHeapVirtualAddrStart, KERNEL_HEAP_PAGES);
        Console.printf_("Kernel heap mapped to virtual address range: %p - %p\n", KernelHeapVirtualAddrStart, KernelHeapVirtualAddrEnd);

        kmemset((void*) KernelHeapVirtualAddrStart, 0, KERNEL_HEAP_PAGES * PAGE_SIZE);

        DispatcherParameters Params = {&PMM, &VMM, &Console, (uint64_t) KernelHeapVirtualAddrStart, (uint64_t) KernelHeapVirtualAddrEnd, KernelArgs.InitramfsAddress, KernelArgs.InitramfsSize};
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

        // Create Null Process (idle process)
        ActiveDispatcher->GetLogicLayer()->CreateNullProcess();

        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("Creating kernel sleep test processes\n");
        KernelTaskAId = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelTaskA);
        KernelTaskBId = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelTaskB);
        KernelTaskCId = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelTaskC);
        KernelTaskDId = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelTaskD);

        if (KernelTaskAId == 0xFF || KernelTaskBId == 0xFF || KernelTaskCId == 0xFF || KernelTaskDId == 0xFF)
        {
            ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("Failed to create kernel test processes\n");
            while (1)
                __asm__ __volatile__("hlt");
        }

        uint64_t InitFileSize = 0;
        void*    InitFileData = ActiveDispatcher->GetResourceLayer()->LoadFileFromInitramfs("/init", &InitFileSize);
        if (InitFileData == nullptr || InitFileSize == 0)
        {
            ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("Failed to load /init from initramfs\n");
        }
        else
        {
            ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("/init loaded from initramfs at %p (%llu bytes)\n", InitFileData, (unsigned long long) InitFileSize);
            ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(reinterpret_cast<void (*)()>(InitFileData));
        }

        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("Switching to Task A (id=%u), Task B (id=%u), Task C (id=%u), Task D (id=%u)\n", KernelTaskAId, KernelTaskBId, KernelTaskCId,
                                                                    KernelTaskDId);
        ActiveDispatcher->GetLogicLayer()->EnableScheduling();

        while (1)
            __asm__ __volatile__("hlt");
    }
}
