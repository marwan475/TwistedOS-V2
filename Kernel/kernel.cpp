
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

#define KERNEL_SLEEP_FAST_TICKS 40
#define KERNEL_SLEEP_MEDIUM_TICKS 130
#define KERNEL_SLEEP_SLOW_TICKS 310
#define KERNEL_BURST_SLEEP_TICKS 15
#define KERNEL_MONITOR_TICKS 500
#define USER_PROCESS_INSTANCE_COUNT 2

extern "C" void DispatcherEntry(DispatcherParameters Params);

extern "C" void EFIAPI KernelEntry(KernelParameters KernelArgs) __attribute__((section(".text.entry")));

static Dispatcher KernelDispatcher;
static uint8_t    KernelSleepFastId = 0xFF;
static uint8_t    KernelSleepMediumId = 0xFF;
static uint8_t    KernelSleepSlowId = 0xFF;
static uint8_t    KernelBurstId = 0xFF;
static uint8_t    KernelMonitorId = 0xFF;

static Dispatcher* RequireActiveDispatcher()
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        while (1)
            __asm__ __volatile__("hlt");
    }

    return ActiveDispatcher;
}

static void KernelSleepFastTask()
{
    Dispatcher* ActiveDispatcher = RequireActiveDispatcher();

    uint64_t Cycle = 0;
    while (1)
    {
        ++Cycle;
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[KernelFastSleep] cycle=%llu sleep=%u\n", (unsigned long long) Cycle, (unsigned) KERNEL_SLEEP_FAST_TICKS);
        ActiveDispatcher->GetLogicLayer()->SleepProcess(KernelSleepFastId, KERNEL_SLEEP_FAST_TICKS);
    }
}

static void KernelSleepMediumTask()
{
    Dispatcher* ActiveDispatcher = RequireActiveDispatcher();

    uint64_t Cycle = 0;
    while (1)
    {
        ++Cycle;
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[KernelMediumSleep] cycle=%llu sleep=%u\n", (unsigned long long) Cycle, (unsigned) KERNEL_SLEEP_MEDIUM_TICKS);
        ActiveDispatcher->GetLogicLayer()->SleepProcess(KernelSleepMediumId, KERNEL_SLEEP_MEDIUM_TICKS);
    }
}

static void KernelSleepSlowTask()
{
    Dispatcher* ActiveDispatcher = RequireActiveDispatcher();

    uint64_t Cycle = 0;
    while (1)
    {
        ++Cycle;
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[KernelSlowSleep] cycle=%llu sleep=%u\n", (unsigned long long) Cycle, (unsigned) KERNEL_SLEEP_SLOW_TICKS);
        ActiveDispatcher->GetLogicLayer()->SleepProcess(KernelSleepSlowId, KERNEL_SLEEP_SLOW_TICKS);
    }
}

static void KernelBurstTask()
{
    Dispatcher* ActiveDispatcher = RequireActiveDispatcher();

    uint64_t Iteration = 0;
    while (1)
    {
        ++Iteration;

        if ((Iteration % 1000) == 0)
        {
            ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[KernelBurst] iteration=%llu\n", (unsigned long long) Iteration);
        }

        if ((Iteration % 1500) == 0)
        {
            ActiveDispatcher->GetLogicLayer()->SleepProcess(KernelBurstId, KERNEL_BURST_SLEEP_TICKS);
        }

        __asm__ __volatile__("pause");
    }
}

static void KernelMonitorTask()
{
    Dispatcher* ActiveDispatcher = RequireActiveDispatcher();

    uint64_t Cycle = 0;
    while (1)
    {
        ++Cycle;
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_(
                "[KernelMonitor] cycle=%llu ids{fast=%u medium=%u slow=%u burst=%u monitor=%u} ticks=%llu\n", (unsigned long long) Cycle, KernelSleepFastId,
                KernelSleepMediumId, KernelSleepSlowId, KernelBurstId, KernelMonitorId, (unsigned long long) Ticks);
        ActiveDispatcher->GetLogicLayer()->SleepProcess(KernelMonitorId, KERNEL_MONITOR_TICKS);
    }
}

static uint8_t CreateUserProcessFromInitramfs(Dispatcher* ActiveDispatcher, const char* Path)
{
    uint64_t FileSize = 0;
    void*    FileData = ActiveDispatcher->GetResourceLayer()->LoadFileFromInitramfs(Path, &FileSize);
    if (FileData == nullptr || FileSize == 0)
    {
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("Failed to load %s from initramfs\n", Path);
        return 0xFF;
    }

    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("%s loaded from initramfs at %p (%llu bytes)\n", Path, FileData, (unsigned long long) FileSize);

    uint8_t UserProcessId = ActiveDispatcher->GetLogicLayer()->CreateUserProcess(reinterpret_cast<uint64_t>(FileData), static_cast<uint64_t>(FileSize));
    if (UserProcessId == 0xFF)
    {
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("Failed to create user process for %s\n", Path);
    }
    else
    {
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("Created %s user process (id=%u)\n", Path, UserProcessId);
    }

    return UserProcessId;
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

        UINTN KernelHeapVirtualAddrEnd = VMM.MapRange((UINTN) KernelHeapPhysicalAddr, KernelHeapVirtualAddrStart, KERNEL_HEAP_PAGES, false);
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

        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("Creating scheduler stress test processes\n");

        KernelSleepFastId   = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelSleepFastTask);
        KernelSleepMediumId = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelSleepMediumTask);
        KernelSleepSlowId   = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelSleepSlowTask);
        KernelBurstId       = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelBurstTask);
        KernelMonitorId     = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelMonitorTask);

        if (KernelSleepFastId == 0xFF || KernelSleepMediumId == 0xFF || KernelSleepSlowId == 0xFF || KernelBurstId == 0xFF || KernelMonitorId == 0xFF)
        {
            ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("Failed to create kernel test processes\n");
            while (1)
                __asm__ __volatile__("hlt");
        }

        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("Creating %u instances of /init and /init2 user processes\n", (unsigned) USER_PROCESS_INSTANCE_COUNT);

        for (uint32_t Index = 0; Index < USER_PROCESS_INSTANCE_COUNT; ++Index)
        {
            if (CreateUserProcessFromInitramfs(ActiveDispatcher, "/init") == 0xFF)
            {
                while (1)
                    __asm__ __volatile__("hlt");
            }

            if (CreateUserProcessFromInitramfs(ActiveDispatcher, "/init2") == 0xFF)
            {
                while (1)
                    __asm__ __volatile__("hlt");
            }

            ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("Created user process pair %u/%u\n", (unsigned) (Index + 1), (unsigned) USER_PROCESS_INSTANCE_COUNT);
        }

        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_(
                "Scheduler stress suite ready: kernel ids {fast=%u medium=%u slow=%u burst=%u monitor=%u}\n", KernelSleepFastId, KernelSleepMediumId, KernelSleepSlowId, KernelBurstId,
                KernelMonitorId);
        ActiveDispatcher->GetLogicLayer()->EnableScheduling();

        while (1)
            __asm__ __volatile__("hlt");
    }
}
