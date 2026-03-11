
#include <font.hpp>
#include <printf.hpp>
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

static uint32_t* framebuffer;
static uint32_t  screen_width;
static uint32_t  screen_height;
static uint32_t  pitch;

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

void fb_init(uint32_t* fb, uint32_t width, uint32_t height, uint32_t scanline)
{
    framebuffer   = fb;
    screen_width  = width;
    screen_height = height;
    pitch         = scanline;
}

static void draw_char_8x16(int x, int y, char c, uint32_t color)
{
    const uint8_t* glyph = &fontdata_8x16.data[(unsigned char) c * FONT_HEIGHT];

    for (int row = 0; row < FONT_HEIGHT; row++)
    {
        uint8_t bits = glyph[row];

        for (int col = 0; col < FONT_WIDTH; col++)
        {
            if (bits & (1 << (7 - col)))
            {
                framebuffer[(y + row) * pitch + (x + col)] = color;
            }
        }
    }
}

void fb_putchar(char c)
{
    if (c == '\n')
    {
        cursor_x = 0;
        cursor_y += FONT_HEIGHT;
        return;
    }

    if (c == '\r')
    {
        cursor_x = 0;
        return;
    }

    draw_char_8x16(cursor_x, cursor_y, c, 0xFFFFFFFF);

    cursor_x += FONT_WIDTH;

    if (cursor_x + FONT_WIDTH >= screen_width)
    {
        cursor_x = 0;
        cursor_y += FONT_HEIGHT;
    }

    if (cursor_y + FONT_HEIGHT >= screen_height)
    {
        cursor_x = 0;
        cursor_y = 0; // simple wrap
    }
}

extern "C" void _putchar(char character)
{
    fb_putchar(character);
}

int kprintf(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    int ret = vprintf_proxy(format, args);

    va_end(args);
    return ret;
}

// Uefi sets us up in 64bit long mode with identity mapped pages
extern "C"
{
    void EFIAPI kernel_main(KernelParameters KernelArgs)
    {
        UINT32* FrameBuffer = (UINT32*) KernelArgs.GopMode.FrameBufferBase;
        UINT32  stride      = KernelArgs.GopMode.Info->PixelsPerScanLine;
        UINT32  xres        = KernelArgs.GopMode.Info->HorizontalResolution;
        UINT32  yres        = KernelArgs.GopMode.Info->VerticalResolution;

        for (UINT32 y = 0; y < yres; y++)
        {
            for (UINT32 x = 0; x < xres; x++)
            {
                FrameBuffer[y * stride + x] = 0x00000000; // 0xAARRGGBB
            }
        }

        fb_init((uint32_t*) KernelArgs.GopMode.FrameBufferBase, KernelArgs.GopMode.Info->HorizontalResolution,
                KernelArgs.GopMode.Info->VerticalResolution, KernelArgs.GopMode.Info->PixelsPerScanLine);

        kprintf(" Hello kernel\n");
        kprintf("Framebuffer console working\n");
        kprintf("Testing integers: %d, %u, %ld, %lu\n", -42, 42U, 123456789L, 987654321UL);
        kprintf("Testing hex: 0x%x, 0x%lx\n", 0xDEADBEAF, 0xCAFEBABECAFEBABEUL);
        kprintf("Testing pointer: %p\n", (void*) 0x12345678ABCDEF00);
        kprintf("Testing characters: %c %c %c\n", 'A', 'B', 'C');
        kprintf("Testing string: %s\n", "Framebuffer printf works!");

        while (1)
            __asm__ __volatile__("hlt");
    }
}
