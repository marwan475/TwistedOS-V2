/**
 * File: TTY.cpp
 * Author: Marwan Mostafa
 * Description: TTY device implementation for buffered keyboard input and console output.
 */

#include "TTY.hpp"

#include <Layers/Logic/VirtualFileSystem.hpp>
#include <Logging/font.hpp>
#include <printf.hpp>
#include <stdarg.h>

namespace
{
#ifdef DEBUG_BUILD
constexpr uint16_t COM1_PORT = 0x3F8;
#endif

constexpr int64_t LINUX_ERR_EFAULT = -14;
constexpr int64_t LINUX_ERR_EINVAL = -22;
constexpr int64_t LINUX_ERR_ENODEV = -19;
constexpr int64_t LINUX_ERR_ENOSPC = -28;
constexpr int64_t LINUX_ERR_ENOSYS = -38;

#ifdef DEBUG_BUILD
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

static void SerialWriteChar(char Character)
{
    while ((inb(COM1_PORT + 5) & 0x20) == 0)
    {
    }

    outb(COM1_PORT, static_cast<uint8_t>(Character));
}

static void SerialWriteBuffer(const char* Buffer, int Count)
{
    if (Buffer == nullptr || Count <= 0)
    {
        return;
    }

    for (int Index = 0; Index < Count; ++Index)
    {
        if (Buffer[Index] == '\n')
        {
            SerialWriteChar('\r');
        }

        SerialWriteChar(Buffer[Index]);
    }
}
#endif
} // namespace

FileOperations TTY::TerminalFileOperations = {
    &TTY::ReadFileOperation,
    &TTY::WriteFileOperation,
    &TTY::SeekFileOperation,
    &TTY::MemoryMapFileOperation,
};

TTY::TTY(FrameBuffer* FrameBuffer, uint32_t InitialCursorX, uint32_t InitialCursorY)
        : FrameBufferDevice(FrameBuffer), CursorX(InitialCursorX), CursorY(InitialCursorY), TextColor(0xFFFFFFFF), BackgroundColor(0x00000000), BufferHead(0), BufferTail(0),
            BufferedBytes(0)
{
    for (uint64_t Index = 0; Index < KEYBOARD_BUFFER_CAPACITY; ++Index)
    {
        KeyboardBuffer[Index] = 0;
    }
}

int TTY::printf_(const char* Format, ...)
{
    if (Format == nullptr)
    {
        return 0;
    }

    char Buffer[512] = {};

    va_list Args;
    va_start(Args, Format);
    int Result = vsnprintf_(Buffer, sizeof(Buffer), Format, Args);
    va_end(Args);

    if (Result <= 0)
    {
        return Result;
    }

    int WritableCount = Result;
    if (WritableCount > static_cast<int>(sizeof(Buffer) - 1))
    {
        WritableCount = static_cast<int>(sizeof(Buffer) - 1);
    }

#ifdef DEBUG_BUILD
    static bool SerialInitialized = false;
    if (!SerialInitialized)
    {
        SerialInit();
        SerialInitialized = true;
    }

    SerialWriteBuffer(Buffer, WritableCount);
#endif

    for (int Index = 0; Index < WritableCount; ++Index)
    {
        PutChar(Buffer[Index]);
    }

    return Result;
}

void TTY::ClearScreen()
{
    if (FrameBufferDevice == nullptr || !FrameBufferDevice->IsValid())
    {
        return;
    }

    for (uint32_t Y = 0; Y < FrameBufferDevice->GetHeight(); ++Y)
    {
        for (uint32_t X = 0; X < FrameBufferDevice->GetWidth(); ++X)
        {
            FrameBufferDevice->Draw(X, Y, BackgroundColor);
        }
    }

    CursorX = 0;
    CursorY = 0;
}

void TTY::DrawChar(uint32_t X, uint32_t Y, char Character)
{
    if (FrameBufferDevice == nullptr || !FrameBufferDevice->IsValid())
    {
        return;
    }

    const uint8_t* Glyph = &fontdata_8x16.data[static_cast<uint8_t>(Character) * FONT_HEIGHT];

    for (uint32_t Row = 0; Row < FONT_HEIGHT; ++Row)
    {
        uint8_t Bits = Glyph[Row];

        for (uint32_t Col = 0; Col < FONT_WIDTH; ++Col)
        {
            uint32_t PixelColor = (Bits & (1U << (7U - Col))) != 0 ? TextColor : BackgroundColor;
            FrameBufferDevice->Draw(X + Col, Y + Row, PixelColor);
        }
    }
}

void TTY::PutChar(char Character)
{
    if (FrameBufferDevice == nullptr || !FrameBufferDevice->IsValid())
    {
        return;
    }

    if (Character == '\n')
    {
        CursorX = 0;
        CursorY += FONT_HEIGHT;
        if (CursorY + FONT_HEIGHT >= FrameBufferDevice->GetHeight())
        {
            ClearScreen();
        }

        return;
    }

    if (Character == '\r')
    {
        CursorX = 0;
        return;
    }

    if (Character == '\b')
    {
        if (CursorX >= FONT_WIDTH)
        {
            CursorX -= FONT_WIDTH;
        }
        DrawChar(CursorX, CursorY, ' ');
        return;
    }

    DrawChar(CursorX, CursorY, Character);

    CursorX += FONT_WIDTH;

    if (CursorX + FONT_WIDTH >= FrameBufferDevice->GetWidth())
    {
        CursorX = 0;
        CursorY += FONT_HEIGHT;
    }

    if (CursorY + FONT_HEIGHT >= FrameBufferDevice->GetHeight())
    {
        ClearScreen();
    }
}

int64_t TTY::Read(File* OpenFile, void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr || (Buffer == nullptr && Count != 0))
    {
        return LINUX_ERR_EFAULT;
    }

    if (Count == 0)
    {
        return 0;
    }

    if (BufferedBytes == 0)
    {
        return 0;
    }

    char*    OutBuffer   = reinterpret_cast<char*>(Buffer);
    uint64_t BytesCopied = 0;

    while (BytesCopied < Count && BufferedBytes > 0)
    {
        OutBuffer[BytesCopied] = KeyboardBuffer[BufferTail];
        BufferTail             = (BufferTail + 1) % KEYBOARD_BUFFER_CAPACITY;
        --BufferedBytes;
        ++BytesCopied;
    }

    OpenFile->CurrentOffset += BytesCopied;
    return static_cast<int64_t>(BytesCopied);
}

int64_t TTY::Write(File* OpenFile, const void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr || (Buffer == nullptr && Count != 0))
    {
        return LINUX_ERR_EFAULT;
    }

    if (Count == 0)
    {
        return 0;
    }

    if (FrameBufferDevice == nullptr || !FrameBufferDevice->IsValid())
    {
        return LINUX_ERR_ENODEV;
    }

    const char* InBuffer = reinterpret_cast<const char*>(Buffer);
    for (uint64_t Index = 0; Index < Count; ++Index)
    {
        PutChar(InBuffer[Index]);
    }

    OpenFile->CurrentOffset += Count;
    return static_cast<int64_t>(Count);
}

uint64_t TTY::PushKeyboardInput(const char* Buffer, uint64_t Count)
{
    if (Buffer == nullptr || Count == 0)
    {
        return 0;
    }

    uint64_t BytesStored = 0;
    while (BytesStored < Count)
    {
        if (BufferedBytes >= KEYBOARD_BUFFER_CAPACITY)
        {
            break;
        }

        KeyboardBuffer[BufferHead] = Buffer[BytesStored];
        BufferHead                 = (BufferHead + 1) % KEYBOARD_BUFFER_CAPACITY;
        ++BufferedBytes;
        ++BytesStored;
    }

    return BytesStored;
}

uint64_t TTY::PushKeyboardInputChar(char Character)
{
    return PushKeyboardInput(&Character, 1);
}

FileOperations* TTY::GetFileOperations()
{
    return &TerminalFileOperations;
}

int64_t TTY::ReadFileOperation(File* OpenFile, void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    TTY* Terminal = reinterpret_cast<TTY*>(OpenFile->Node->NodeData);
    if (Terminal == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    return Terminal->Read(OpenFile, Buffer, Count);
}

int64_t TTY::WriteFileOperation(File* OpenFile, const void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    TTY* Terminal = reinterpret_cast<TTY*>(OpenFile->Node->NodeData);
    if (Terminal == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    return Terminal->Write(OpenFile, Buffer, Count);
}

int64_t TTY::SeekFileOperation(File* OpenFile, int64_t Offset, int32_t Whence)
{
    (void) OpenFile;
    (void) Offset;
    (void) Whence;
    return LINUX_ERR_ENOSYS;
}

int64_t TTY::MemoryMapFileOperation(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address)
{
    (void) OpenFile;
    (void) Length;
    (void) Offset;
    (void) AddressSpace;
    (void) Address;
    return LINUX_ERR_ENOSYS;
}
