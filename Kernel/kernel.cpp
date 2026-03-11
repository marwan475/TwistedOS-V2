
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

        UINTN TotalUsableMemoryBytes = PMM.GetTotalUsableMemory();
        UINTN TotalUsableMemoryMiB   = TotalUsableMemoryBytes / (1024 * 1024);
        UINTN TotalUsableMemoryGiB   = TotalUsableMemoryMiB / 1024;
        Console.printf_("Total usable memory: %llu bytes (%llu MiB / %llu GiB)\n",
                        (unsigned long long) TotalUsableMemoryBytes, (unsigned long long) TotalUsableMemoryMiB,
                        (unsigned long long) TotalUsableMemoryGiB);

        while (1)
            __asm__ __volatile__("hlt");
    }
}
