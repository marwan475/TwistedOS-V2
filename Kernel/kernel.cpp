
#include <Arch/x86.hpp>
#include <Logging/FrameBufferConsole.hpp>
#include <PhysicalMemoryManager.hpp>
#include <VirtualMemoryManager.hpp>
#include "../utils/KernelParameters.hpp"
#include <stdint.h>

static void RunMemoryManagersIntegrationTest(FrameBufferConsole& Console, PhysicalMemoryManager& PMM,
                                             VirtualMemoryManager& VMM)
{
    void* PhysicalPage = PMM.AllocateFromDescriptor(1);
    Console.printf_("IntegrationTest: AllocateFromDescriptor(1) -> 0x%llx\n",
                    (unsigned long long) (uint64_t) PhysicalPage);

    if (PhysicalPage == NULL)
    {
        Console.printf_("IntegrationTest: FAIL (allocation failed)\n");
        return;
    }

    UINTN TestVirtualAddr = 0xFFFF800000200000;
    bool  Mapped          = VMM.MapPage((UINTN) PhysicalPage, TestVirtualAddr);
    Console.printf_("IntegrationTest: MapPage(0x%llx -> 0x%llx) -> %s\n",
                    (unsigned long long) (uint64_t) PhysicalPage, (unsigned long long) TestVirtualAddr,
                    Mapped ? "true" : "false");

    bool Unmapped = Mapped ? VMM.UnmapPage(TestVirtualAddr) : false;
    Console.printf_("IntegrationTest: UnmapPage(0x%llx) -> %s\n", (unsigned long long) TestVirtualAddr,
                    Unmapped ? "true" : "false");

    bool Freed = PMM.FreeFromDescriptor(PhysicalPage, 1);
    Console.printf_("IntegrationTest: FreeFromDescriptor(0x%llx, 1) -> %s\n",
                    (unsigned long long) (uint64_t) PhysicalPage, Freed ? "true" : "false");

    bool Passed = Mapped && Unmapped && Freed;
    Console.printf_("IntegrationTest: %s\n", Passed ? "PASS" : "FAIL");
}

extern "C" void EFIAPI kernel_main(KernelParameters KernelArgs) __attribute__((section(".text.entry")));

// Uefi sets us up in 64bit long mode with identity mapped pages
extern "C"
{
    void EFIAPI kernel_main(KernelParameters KernelArgs)
    {
        FrameBufferConsole Console;
        Console.Initialize((uint32_t*) KernelArgs.GopMode.FrameBufferBase,
                           KernelArgs.GopMode.Info->HorizontalResolution, KernelArgs.GopMode.Info->VerticalResolution,
                           KernelArgs.GopMode.Info->PixelsPerScanLine);
        Console.Clear();
        FrameBufferConsole::SetActive(&Console);
        Console.printf_("Framebuffer console Initialized\n");

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

        RunMemoryManagersIntegrationTest(Console, PMM, VMM);
        PMM.PrintMemoryDescriptors(Console);

        while (1)
            __asm__ __volatile__("hlt");
    }
}
