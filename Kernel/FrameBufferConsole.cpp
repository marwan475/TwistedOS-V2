#include <FrameBufferConsole.hpp>
#include <font.hpp>
#include <printf.hpp>

FrameBufferConsole* FrameBufferConsole::ActiveConsole = nullptr;

FrameBufferConsole::FrameBufferConsole()
    : framebuffer(nullptr), screenWidth(0), screenHeight(0), pitch(0), cursorX(0), cursorY(0), textColor(0xFFFFFFFF)
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
        cursorX = 0;
        cursorY = 0;
    }
}

int FrameBufferConsole::printf_(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    int ret = vprintf_proxy(format, args);

    va_end(args);
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