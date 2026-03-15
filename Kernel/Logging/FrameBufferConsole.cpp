#include <Logging/FrameBufferConsole.hpp>
#include <Logging/font.hpp>
#include <printf.hpp>

namespace
{
constexpr uint16_t COM1_PORT = 0x3F8;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value = 0;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void SerialInit()
{
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

static void SerialWriteChar(char character)
{
    while ((inb(COM1_PORT + 5) & 0x20) == 0)
    {
    }

    outb(COM1_PORT, (uint8_t) character);
}

static void SerialPrintfOut(char character, void* arg)
{
    (void) arg;

    if (character == '\n')
    {
        SerialWriteChar('\r');
    }

    SerialWriteChar(character);
}
} // namespace

FrameBufferConsole* FrameBufferConsole::ActiveConsole = nullptr;

FrameBufferConsole::FrameBufferConsole() : framebuffer(nullptr), screenWidth(0), screenHeight(0), pitch(0), cursorX(0), cursorY(0), textColor(0xFFFFFFFF)
{
}

void FrameBufferConsole::Initialize(uint32_t* buffer, uint32_t width, uint32_t height, uint32_t scanline)
{
    framebuffer  = buffer;
    screenWidth  = width;
    screenHeight = height;
    pitch        = scanline;
    cursorX      = 0;
    cursorY      = 0;
}

void FrameBufferConsole::Clear(uint32_t color)
{
    if (framebuffer == nullptr)
    {
        return;
    }

    for (uint32_t y = 0; y < screenHeight; y++)
    {
        for (uint32_t x = 0; x < screenWidth; x++)
        {
            framebuffer[y * pitch + x] = color;
        }
    }

    cursorX = 0;
    cursorY = 0;
}

void FrameBufferConsole::DrawChar(int x, int y, char c)
{
    if (framebuffer == nullptr)
    {
        return;
    }

    const uint8_t* glyph = &fontdata_8x16.data[(unsigned char) c * FontHeight];

    for (uint32_t row = 0; row < FontHeight; row++)
    {
        uint8_t bits = glyph[row];

        for (uint32_t col = 0; col < FontWidth; col++)
        {
            if (bits & (1 << (7 - col)))
            {
                framebuffer[(y + row) * pitch + (x + col)] = textColor;
            }
        }
    }
}

void FrameBufferConsole::PutChar(char c)
{
    if (c == '\n')
    {
        cursorX = 0;
        cursorY += FontHeight;

        if (cursorY + FontHeight >= screenHeight)
        {
            Clear();
        }

        return;
    }

    if (c == '\r')
    {
        cursorX = 0;
        return;
    }

    DrawChar((int) cursorX, (int) cursorY, c);

    cursorX += FontWidth;

    if (cursorX + FontWidth >= screenWidth)
    {
        cursorX = 0;
        cursorY += FontHeight;
    }

    if (cursorY + FontHeight >= screenHeight)
    {
        Clear();
    }
}

int FrameBufferConsole::printf_(const char* format, ...)
{
    va_list args;
    va_start(args, format);

#ifdef DEBUG_BUILD
    va_list debugArgs;
    va_copy(debugArgs, args);
#endif

    int ret = vprintf_proxy(format, args);

#ifdef DEBUG_BUILD
    char buffer[512] = {};
    int  debugRet    = vsnprintf_(buffer, sizeof(buffer), format, debugArgs);
    va_end(debugArgs);

    if (debugRet > 0)
    {
        for (int i = 0; i < debugRet && i < (int) (sizeof(buffer) - 1); i++)
        {
            SerialPrintfOut(buffer[i], nullptr);
        }
    }
#endif

    va_end(args);
    return ret;
}

int FrameBufferConsole::dbgprintf_(const char* format, ...)
{
    static bool SerialInitialized = false;
    if (!SerialInitialized)
    {
        SerialInit();
        SerialInitialized = true;
    }

    va_list args;
    va_start(args, format);

    char buffer[512] = {};
    int  ret         = vsnprintf_(buffer, sizeof(buffer), format, args);

    va_end(args);

    if (ret < 0)
    {
        return ret;
    }

    for (int i = 0; i < ret && i < (int) (sizeof(buffer) - 1); i++)
    {
        SerialPrintfOut(buffer[i], nullptr);
    }

    return ret;
}

void FrameBufferConsole::SetActive(FrameBufferConsole* console)
{
    ActiveConsole = console;
}

FrameBufferConsole* FrameBufferConsole::GetActive()
{
    return ActiveConsole;
}

extern "C" void _putchar(char character)
{
    FrameBufferConsole* console = FrameBufferConsole::GetActive();

    if (console != nullptr)
    {
        console->PutChar(character);
    }
}