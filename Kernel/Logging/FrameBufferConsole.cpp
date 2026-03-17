/**
 * File: FrameBufferConsole.cpp
 * Author: Marwan Mostafa
 * Description: Framebuffer-backed kernel console implementation.
 */

#include <Logging/FrameBufferConsole.hpp>
#include <Logging/font.hpp>
#include <printf.hpp>

namespace
{
constexpr uint16_t COM1_PORT = 0x3F8;

/**
 * Function: outb
 * Description: Writes one byte to an I/O port.
 * Parameters:
 *   uint16_t port - Target hardware I/O port.
 *   uint8_t value - Byte value to write.
 * Returns:
 *   void - No return value.
 */
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * Function: inb
 * Description: Reads one byte from an I/O port.
 * Parameters:
 *   uint16_t port - Source hardware I/O port.
 * Returns:
 *   uint8_t - Byte read from the port.
 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t value = 0;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/**
 * Function: SerialInit
 * Description: Initializes COM1 serial port with 38400 baud, 8N1 configuration.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
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

/**
 * Function: SerialWriteChar
 * Description: Writes one character to COM1 after transmit buffer becomes ready.
 * Parameters:
 *   char character - Character to write.
 * Returns:
 *   void - No return value.
 */
static void SerialWriteChar(char character)
{
    while ((inb(COM1_PORT + 5) & 0x20) == 0)
    {
    }

    outb(COM1_PORT, (uint8_t) character);
}

/**
 * Function: SerialPrintfOut
 * Description: Character output callback that normalizes newlines for serial output.
 * Parameters:
 *   char character - Character produced by formatted output routines.
 *   void* arg - Unused callback context pointer.
 * Returns:
 *   void - No return value.
 */
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

/**
 * Function: FrameBufferConsole::FrameBufferConsole
 * Description: Constructs a framebuffer console with default empty state.
 * Parameters:
 *   None
 * Returns:
 *   FrameBufferConsole - Constructed console instance.
 */
FrameBufferConsole::FrameBufferConsole() : framebuffer(nullptr), screenWidth(0), screenHeight(0), pitch(0), cursorX(0), cursorY(0), textColor(0xFFFFFFFF)
{
}

/**
 * Function: FrameBufferConsole::Initialize
 * Description: Initializes framebuffer properties and resets the text cursor.
 * Parameters:
 *   uint32_t* buffer - Pointer to framebuffer pixel memory.
 *   uint32_t width - Screen width in pixels.
 *   uint32_t height - Screen height in pixels.
 *   uint32_t scanline - Number of pixels per framebuffer row.
 * Returns:
 *   void - No return value.
 */
void FrameBufferConsole::Initialize(uint32_t* buffer, uint32_t width, uint32_t height, uint32_t scanline)
{
    framebuffer  = buffer;
    screenWidth  = width;
    screenHeight = height;
    pitch        = scanline;
    cursorX      = 0;
    cursorY      = 0;
}

/**
 * Function: FrameBufferConsole::Clear
 * Description: Fills the framebuffer with a color and resets the cursor position.
 * Parameters:
 *   uint32_t color - ARGB/RGBA color value used to clear the screen.
 * Returns:
 *   void - No return value.
 */
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

/**
 * Function: FrameBufferConsole::DrawChar
 * Description: Draws a single glyph at pixel coordinates using the active text color.
 * Parameters:
 *   int x - Left pixel coordinate.
 *   int y - Top pixel coordinate.
 *   char c - Character to render.
 * Returns:
 *   void - No return value.
 */
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

/**
 * Function: FrameBufferConsole::PutChar
 * Description: Writes one character at cursor position and advances with wrapping behavior.
 * Parameters:
 *   char c - Character to print.
 * Returns:
 *   void - No return value.
 */
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

/**
 * Function: FrameBufferConsole::printf_
 * Description: Prints formatted text to the active framebuffer console and optional debug serial output.
 * Parameters:
 *   const char* format - printf-style format string.
 *   ... - Format arguments.
 * Returns:
 *   int - Number of characters written by the proxy formatter.
 */
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

/**
 * Function: FrameBufferConsole::dbgprintf_
 * Description: Prints formatted text to COM1 serial debug output.
 * Parameters:
 *   const char* format - printf-style format string.
 *   ... - Format arguments.
 * Returns:
 *   int - Number of characters generated, or negative value on formatting error.
 */
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

/**
 * Function: FrameBufferConsole::SetActive
 * Description: Sets the global active console instance used by low-level output hooks.
 * Parameters:
 *   FrameBufferConsole* console - Console instance to mark as active.
 * Returns:
 *   void - No return value.
 */
void FrameBufferConsole::SetActive(FrameBufferConsole* console)
{
    ActiveConsole = console;
}

/**
 * Function: FrameBufferConsole::GetActive
 * Description: Returns the current global active console instance.
 * Parameters:
 *   None
 * Returns:
 *   FrameBufferConsole* - Pointer to active console, or nullptr if not set.
 */
FrameBufferConsole* FrameBufferConsole::GetActive()
{
    return ActiveConsole;
}

/**
 * Function: _putchar
 * Description: C-linkage character output hook forwarding characters to the active framebuffer console.
 * Parameters:
 *   char character - Character to output.
 * Returns:
 *   void - No return value.
 */
extern "C" void _putchar(char character)
{
    FrameBufferConsole* console = FrameBufferConsole::GetActive();

    if (console != nullptr)
    {
        console->PutChar(character);
    }
}