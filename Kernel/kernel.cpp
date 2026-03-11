
#include <FrameBufferConsole.hpp>
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
} KernelParameters;

extern "C" void EFIAPI kernel_main(KernelParameters KernelArgs) __attribute__((section(".text.entry")));

// Uefi sets us up in 64bit long mode with identity mapped pages
extern "C"
{
    void EFIAPI kernel_main(KernelParameters KernelArgs)
    {
        FrameBufferConsole Console;
        Console.Initialize((uint32_t*) KernelArgs.GopMode.FrameBufferBase, KernelArgs.GopMode.Info->HorizontalResolution,
                           KernelArgs.GopMode.Info->VerticalResolution, KernelArgs.GopMode.Info->PixelsPerScanLine);
        Console.Clear();
        FrameBufferConsole::SetActive(&Console);

        Console.printf_("Hello kernel\n");
        Console.printf_("Framebuffer console working\n");
        Console.printf_("Testing integers: %d, %u, %ld, %lu\n", -42, 42U, 123456789L, 987654321UL);
        Console.printf_("Testing hex: 0x%x, 0x%lx\n", 0xDEADBEAF, 0xCAFEBABECAFEBABEUL);
        Console.printf_("Testing pointer: %p\n", (void*) 0x12345678ABCDEF00);
        Console.printf_("Testing characters: %c %c %c\n", 'A', 'B', 'C');
        Console.printf_("Testing string: %s\n", "Framebuffer printf works!");

        while (1)
            __asm__ __volatile__("hlt");
    }
}
