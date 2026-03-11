
#include <Arch/x86.hpp>
#include <KernelParameters.hpp>
#include <Logging/FrameBufferConsole.hpp>
#include <PhysicalMemoryManager.hpp>
#include <stdint.h>

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
        PMM.PrintConventionalMemoryMap(Console);

        UINTN TotalUsableMemoryBytes = PMM.TotalUsableMemoryBytes();
        UINTN TotalUsableMemoryMiB   = TotalUsableMemoryBytes / (1024 * 1024);
        UINTN TotalUsableMemoryGiB   = TotalUsableMemoryMiB / 1024;
        Console.printf_("Total usable memory: %llu bytes (%llu MiB / %llu GiB)\n",
                        (unsigned long long) TotalUsableMemoryBytes, (unsigned long long) TotalUsableMemoryMiB,
                        (unsigned long long) TotalUsableMemoryGiB);

        UINTN TotalPages = PMM.TotalPages();
        Console.printf_("Total number of pages: %llu\n", (unsigned long long) TotalPages);

        PMM.InitializeMemoryDescriptors();
        Console.printf_("Descriptor initialized\n");
        PMM.PrintMemoryDescriptors(Console);

        void* DescriptorAllocA = PMM.AllocateFromDescriptor(1);
        Console.printf_("AllocateFromDescriptor(1) -> 0x%llx\n", (unsigned long long) (uint64_t) DescriptorAllocA);
        PMM.PrintMemoryDescriptors(Console);

        void* DescriptorAllocB = PMM.AllocateFromDescriptor(4);
        Console.printf_("AllocateFromDescriptor(4) -> 0x%llx\n", (unsigned long long) (uint64_t) DescriptorAllocB);
        PMM.PrintMemoryDescriptors(Console);

        bool FreedAllocA = PMM.FreeFromDescriptor(DescriptorAllocA, 1);
        Console.printf_("FreeFromDescriptor(0x%llx, 1) -> %s\n", (unsigned long long) (uint64_t) DescriptorAllocA,
                        FreedAllocA ? "true" : "false");
        PMM.PrintMemoryDescriptors(Console);

        bool FreedAllocB = PMM.FreeFromDescriptor(DescriptorAllocB, 4);
        Console.printf_("FreeFromDescriptor(0x%llx, 4) -> %s\n", (unsigned long long) (uint64_t) DescriptorAllocB,
                        FreedAllocB ? "true" : "false");
        PMM.PrintMemoryDescriptors(Console);

        while (1)
            __asm__ __volatile__("hlt");
    }
}
