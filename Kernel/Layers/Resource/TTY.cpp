/**
 * File: TTY.cpp
 * Author: Marwan Mostafa
 * Description: TTY device implementation for buffered keyboard input and console output.
 */

#include "TTY.hpp"

#include <Layers/Dispatcher.hpp>
#include <Layers/Logic/LogicLayer.hpp>
#include <Layers/Logic/VirtualFileSystem.hpp>
#include <Logging/font.hpp>
#include <printf.hpp>
#include <stdarg.h>

namespace
{
constexpr uint16_t COM1_PORT = 0x3F8;

constexpr int64_t LINUX_ERR_EFAULT = -14;
constexpr int64_t LINUX_ERR_EINVAL = -22;
constexpr int64_t LINUX_ERR_ENODEV = -19;
constexpr int64_t LINUX_ERR_ENOSPC = -28;
constexpr int64_t LINUX_ERR_ENOSYS = -38;
constexpr int64_t LINUX_ERR_ENOTTY = -25;

constexpr uint64_t LINUX_IOCTL_TCGETS    = 0x5401;
constexpr uint64_t LINUX_IOCTL_TCSETS    = 0x5402;
constexpr uint64_t LINUX_IOCTL_TCSETSW   = 0x5403;
constexpr uint64_t LINUX_IOCTL_TCSETSF   = 0x5404;
constexpr uint64_t LINUX_IOCTL_TIOCGWINSZ = 0x5413;
constexpr uint64_t LINUX_IOCTL_TIOCSWINSZ = 0x5414;
constexpr uint64_t LINUX_IOCTL_FIONREAD   = 0x541B;

struct LinuxTermios
{
    uint32_t InputFlags;
    uint32_t OutputFlags;
    uint32_t ControlFlags;
    uint32_t LocalFlags;
    uint8_t  LineDiscipline;
    uint8_t  ControlCharacters[19];
};

struct LinuxWinSize
{
    uint16_t Rows;
    uint16_t Columns;
    uint16_t XPixel;
    uint16_t YPixel;
};

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

static void EnsureSerialInitialized()
{
    static bool SerialInitialized = false;
    if (!SerialInitialized)
    {
        SerialInit();
        SerialInitialized = true;
    }
}
} // namespace

FileOperations TTY::TerminalFileOperations = {
        &TTY::ReadFileOperation,
        &TTY::WriteFileOperation,
        &TTY::SeekFileOperation,
        &TTY::MemoryMapFileOperation,
    &TTY::IoctlFileOperation,
};

TTY::TTY(FrameBuffer* FrameBuffer, uint32_t InitialCursorX, uint32_t InitialCursorY)
    : FrameBufferDevice(FrameBuffer), CursorX(InitialCursorX), CursorY(InitialCursorY), TextColor(0xFFFFFFFF), BackgroundColor(0x00000000), BufferHead(0), BufferTail(0), BufferedBytes(0)
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

    EnsureSerialInitialized();
    SerialWriteBuffer(Buffer, WritableCount);

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

    while (BufferedBytes == 0)
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher == nullptr)
        {
            return 0;
        }

        LogicLayer* ActiveLogicLayer = ActiveDispatcher->GetLogicLayer();
        if (ActiveLogicLayer == nullptr)
        {
            return LINUX_ERR_EFAULT;
        }

        ProcessManager* PM = ActiveLogicLayer->GetProcessManager();
        if (PM == nullptr)
        {
            return LINUX_ERR_EFAULT;
        }

        Process* CurrentProcess = PM->GetRunningProcess();
        if (CurrentProcess == nullptr)
        {
            return LINUX_ERR_EFAULT;
        }

        ActiveLogicLayer->BlockProcessForTTYInput(CurrentProcess->Id);
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

    EnsureSerialInitialized();
    int SerialCount = static_cast<int>(Count);
    if (Count > static_cast<uint64_t>(INT32_MAX))
    {
        SerialCount = INT32_MAX;
    }
    SerialWriteBuffer(InBuffer, SerialCount);

    for (uint64_t Index = 0; Index < Count; ++Index)
    {
        PutChar(InBuffer[Index]);
    }

    OpenFile->CurrentOffset += Count;
    return static_cast<int64_t>(Count);
}

int64_t TTY::Ioctl(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr || Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (Request == LINUX_IOCTL_TCGETS)
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        LinuxTermios Termios = {};
        return Logic->CopyFromKernelToUser(&Termios, reinterpret_cast<void*>(Argument), sizeof(Termios)) ? 0 : LINUX_ERR_EFAULT;
    }

    if (Request == LINUX_IOCTL_TCSETS || Request == LINUX_IOCTL_TCSETSW || Request == LINUX_IOCTL_TCSETSF)
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        LinuxTermios Incoming = {};
        return Logic->CopyFromUserToKernel(reinterpret_cast<const void*>(Argument), &Incoming, sizeof(Incoming)) ? 0 : LINUX_ERR_EFAULT;
    }

    if (Request == LINUX_IOCTL_TIOCGWINSZ)
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        LinuxWinSize WindowSize = {};
        if (FrameBufferDevice != nullptr && FrameBufferDevice->IsValid())
        {
            WindowSize.Columns = static_cast<uint16_t>(FrameBufferDevice->GetWidth() / FONT_WIDTH);
            WindowSize.Rows    = static_cast<uint16_t>(FrameBufferDevice->GetHeight() / FONT_HEIGHT);
            WindowSize.XPixel  = static_cast<uint16_t>(FrameBufferDevice->GetWidth());
            WindowSize.YPixel  = static_cast<uint16_t>(FrameBufferDevice->GetHeight());
        }
        else
        {
            WindowSize.Columns = 80;
            WindowSize.Rows    = 25;
        }

        return Logic->CopyFromKernelToUser(&WindowSize, reinterpret_cast<void*>(Argument), sizeof(WindowSize)) ? 0 : LINUX_ERR_EFAULT;
    }

    if (Request == LINUX_IOCTL_TIOCSWINSZ)
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        LinuxWinSize Incoming = {};
        return Logic->CopyFromUserToKernel(reinterpret_cast<const void*>(Argument), &Incoming, sizeof(Incoming)) ? 0 : LINUX_ERR_EFAULT;
    }

    if (Request == LINUX_IOCTL_FIONREAD)
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        int32_t BytesAvailable = static_cast<int32_t>(BufferedBytes);
        return Logic->CopyFromKernelToUser(&BytesAvailable, reinterpret_cast<void*>(Argument), sizeof(BytesAvailable)) ? 0 : LINUX_ERR_EFAULT;
    }

    return LINUX_ERR_ENOTTY;
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
    if (Character == '\r')
    {
        Character = '\n';
    }

    if (Character == '\b')
    {
        if (BufferedBytes > 0)
        {
            BufferHead = (BufferHead + KEYBOARD_BUFFER_CAPACITY - 1) % KEYBOARD_BUFFER_CAPACITY;
            --BufferedBytes;
        }

        PutChar('\b');
        return 1;
    }

    uint64_t BytesStored = PushKeyboardInput(&Character, 1);
    if (BytesStored == 0)
    {
        return 0;
    }

    PutChar(Character);
    return BytesStored;
}

uint64_t TTY::GetBufferedInputBytes() const
{
    return BufferedBytes;
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

int64_t TTY::IoctlFileOperation(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic)
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

    return Terminal->Ioctl(OpenFile, Request, Argument, Logic);
}
