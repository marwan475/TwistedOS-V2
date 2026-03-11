
#include <Arch/x86.hpp>
#include <Logging/FrameBufferConsole.hpp>
#include <stdint.h>
#include <uefi.hpp>

typedef struct
{
    UINTN                  MemoryMapSize;
    EFI_MEMORY_DESCRIPTOR* MemoryMap;
    UINTN                  MapKey;
    UINTN                  DescriptorSize;
    UINT32                 DescriptorVersion;
} MemoryMapInfo;

typedef struct
{
    MemoryMapInfo                     MemoryMap;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GopMode;
    UINTN                             KernelEndVirtual;
    UINTN                             PageMapL4Table;
} KernelParameters;

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

        RemapPIC();
        Console.printf_("PIC Remapped\n");

        // Enable Interrupts
        asm volatile("sti");
        
        while (1)
            __asm__ __volatile__("hlt");
    }
}
