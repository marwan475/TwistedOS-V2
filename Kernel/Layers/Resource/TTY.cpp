/**
 * File: TTY.cpp
 * Author: Marwan Mostafa
 * Description: TTY device implementation for buffered keyboard input and console output.
 */

#include "TTY.hpp"

#include <Arch/x86.hpp>
#include <CommonUtils.hpp>
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
constexpr int64_t LINUX_ERR_EAGAIN = -11;

constexpr uint64_t LINUX_O_NONBLOCK = 0x800;

constexpr uint64_t LINUX_IOCTL_TCGETS     = 0x5401;
constexpr uint64_t LINUX_IOCTL_TCSETS     = 0x5402;
constexpr uint64_t LINUX_IOCTL_TCSETSW    = 0x5403;
constexpr uint64_t LINUX_IOCTL_TCSETSF    = 0x5404;
constexpr uint64_t LINUX_IOCTL_TIOCGWINSZ = 0x5413;
constexpr uint64_t LINUX_IOCTL_TIOCSWINSZ = 0x5414;
constexpr uint64_t LINUX_IOCTL_FIONREAD   = 0x541B;

constexpr uint32_t LINUX_TERMIOS_ISIG    = 0x00000001;
constexpr uint32_t LINUX_TERMIOS_ICANON  = 0x00000002;
constexpr uint32_t LINUX_TERMIOS_ECHO    = 0x00000008;
constexpr uint32_t LINUX_TERMIOS_DEFAULT = (LINUX_TERMIOS_ISIG | LINUX_TERMIOS_ICANON | LINUX_TERMIOS_ECHO);

constexpr uint8_t LINUX_CC_VINTR  = 0;
constexpr uint8_t LINUX_CC_VERASE = 2;
constexpr uint8_t LINUX_CC_VEOF   = 4;
constexpr uint8_t LINUX_CC_VTIME  = 5;
constexpr uint8_t LINUX_CC_VMIN   = 6;

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

static void SerialInit()
{
    X86OutB(COM1_PORT + 1, 0x00);
    X86OutB(COM1_PORT + 3, 0x80);
    X86OutB(COM1_PORT + 0, 0x03);
    X86OutB(COM1_PORT + 1, 0x00);
    X86OutB(COM1_PORT + 3, 0x03);
    X86OutB(COM1_PORT + 2, 0xC7);
    X86OutB(COM1_PORT + 4, 0x0B);
}

static void SerialWriteChar(char Character)
{
    while ((X86InB(COM1_PORT + 5) & 0x20) == 0)
    {
    }

    X86OutB(COM1_PORT, static_cast<uint8_t>(Character));
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
        &TTY::ReadFileOperation, &TTY::WriteFileOperation, &TTY::SeekFileOperation, &TTY::MemoryMapFileOperation, &TTY::IoctlFileOperation,
};

TTY::TTY(FrameBuffer* FrameBuffer, uint32_t InitialCursorX, uint32_t InitialCursorY)
    : FrameBufferDevice(FrameBuffer), CursorX(InitialCursorX), CursorY(InitialCursorY), TextColor(0xFFFFFFFF), BackgroundColor(0x00000000), BufferHead(0), BufferTail(0), BufferedBytes(0),
      CommittedBytes(0), TermiosInputFlags(0), TermiosOutputFlags(0), TermiosControlFlags(0), TermiosLocalFlags(LINUX_TERMIOS_DEFAULT), TermiosLineDiscipline(0),
      OutputAnsiState(AnsiParseState::Normal), OutputAnsiParams{0, 0, 0, 0}, OutputAnsiParamCount(0), OutputAnsiCurrentValue(0), OutputAnsiReadingValue(false)
{
    for (uint64_t Index = 0; Index < KEYBOARD_BUFFER_CAPACITY; ++Index)
    {
        KeyboardBuffer[Index] = 0;
    }

    for (uint64_t Index = 0; Index < 19; ++Index)
    {
        TermiosControlCharacters[Index] = 0;
    }

    TermiosControlCharacters[LINUX_CC_VINTR]  = 3;
    TermiosControlCharacters[LINUX_CC_VERASE] = 127;
    TermiosControlCharacters[LINUX_CC_VEOF]   = 4;
    TermiosControlCharacters[LINUX_CC_VTIME]  = 0;
    TermiosControlCharacters[LINUX_CC_VMIN]   = 1;
}

void TTY::ResetAnsiParser()
{
    OutputAnsiState        = AnsiParseState::Normal;
    OutputAnsiParamCount   = 0;
    OutputAnsiCurrentValue = 0;
    OutputAnsiReadingValue = false;
    for (uint8_t Index = 0; Index < 4; ++Index)
    {
        OutputAnsiParams[Index] = 0;
    }
}

void TTY::HandleOutputChar(char Character)
{
    const uint8_t Byte = static_cast<uint8_t>(Character);

    if (OutputAnsiState == AnsiParseState::Normal)
    {
        if (Byte == 0x1B)
        {
            OutputAnsiState = AnsiParseState::Escape;
            return;
        }

        PutChar(Character);
        return;
    }

    if (OutputAnsiState == AnsiParseState::Escape)
    {
        if (Character == '[')
        {
            OutputAnsiState        = AnsiParseState::Csi;
            OutputAnsiParamCount   = 0;
            OutputAnsiCurrentValue = 0;
            OutputAnsiReadingValue = false;
            for (uint8_t Index = 0; Index < 4; ++Index)
            {
                OutputAnsiParams[Index] = 0;
            }
            return;
        }

        PutChar(Character);
        ResetAnsiParser();
        return;
    }

    if (OutputAnsiState == AnsiParseState::Csi)
    {
        if (Character >= '0' && Character <= '9')
        {
            OutputAnsiReadingValue = true;
            uint16_t NextValue     = static_cast<uint16_t>(OutputAnsiCurrentValue) * 10U + static_cast<uint16_t>(Character - '0');
            if (NextValue > 255)
            {
                NextValue = 255;
            }
            OutputAnsiCurrentValue = static_cast<uint8_t>(NextValue);
            return;
        }

        if (Character == ';')
        {
            if (OutputAnsiParamCount < 4)
            {
                OutputAnsiParams[OutputAnsiParamCount++] = OutputAnsiReadingValue ? OutputAnsiCurrentValue : 0;
            }
            OutputAnsiCurrentValue = 0;
            OutputAnsiReadingValue = false;
            return;
        }

        if (OutputAnsiReadingValue && OutputAnsiParamCount < 4)
        {
            OutputAnsiParams[OutputAnsiParamCount++] = OutputAnsiCurrentValue;
        }

        if (Character == 'H' || Character == 'f')
        {
            uint8_t Row = 1;
            uint8_t Col = 1;
            if (OutputAnsiParamCount >= 1 && OutputAnsiParams[0] != 0)
            {
                Row = OutputAnsiParams[0];
            }
            if (OutputAnsiParamCount >= 2 && OutputAnsiParams[1] != 0)
            {
                Col = OutputAnsiParams[1];
            }

            uint32_t MaxColumns = FrameBufferDevice->GetWidth() / FONT_WIDTH;
            uint32_t MaxRows    = FrameBufferDevice->GetHeight() / FONT_HEIGHT;

            if (Row > MaxRows)
            {
                Row = static_cast<uint8_t>(MaxRows);
            }
            if (Col > MaxColumns)
            {
                Col = static_cast<uint8_t>(MaxColumns);
            }

            CursorY = static_cast<uint32_t>(Row - 1) * FONT_HEIGHT;
            CursorX = static_cast<uint32_t>(Col - 1) * FONT_WIDTH;
            ResetAnsiParser();
            return;
        }

        if (Character == 'J')
        {
            uint8_t Mode = 0;
            if (OutputAnsiParamCount >= 1)
            {
                Mode = OutputAnsiParams[0];
            }

            if (Mode == 0 || Mode == 2)
            {
                ClearScreen();
            }

            ResetAnsiParser();
            return;
        }

        ResetAnsiParser();
        return;
    }
}

bool TTY::IsCanonicalModeEnabled() const
{
    return (TermiosLocalFlags & LINUX_TERMIOS_ICANON) != 0;
}

bool TTY::IsEchoEnabled() const
{
    return (TermiosLocalFlags & LINUX_TERMIOS_ECHO) != 0;
}

uint8_t TTY::GetControlCharacter(uint64_t Index) const
{
    if (Index >= 19)
    {
        return 0;
    }

    return TermiosControlCharacters[Index];
}

uint64_t TTY::GetReadableBytes() const
{
    if (IsCanonicalModeEnabled())
    {
        return CommittedBytes;
    }

    return BufferedBytes;
}

void TTY::NotifyInputAvailable()
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        return;
    }

    LogicLayer* ActiveLogicLayer = ActiveDispatcher->GetLogicLayer();
    if (ActiveLogicLayer == nullptr)
    {
        return;
    }

    ActiveLogicLayer->NotifyTTYInputAvailable();
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

    bool IsNonBlocking = (OpenFile->OpenFlags & LINUX_O_NONBLOCK) != 0;

    uint8_t VMin  = GetControlCharacter(LINUX_CC_VMIN);
    uint8_t VTime = GetControlCharacter(LINUX_CC_VTIME);

    uint64_t RequiredBytes = 1;
    if (!IsCanonicalModeEnabled())
    {
        RequiredBytes = (VMin == 0) ? 1 : static_cast<uint64_t>(VMin);
    }

    if (!IsCanonicalModeEnabled() && VMin == 0 && VTime == 0)
    {
        RequiredBytes = 0;
    }

    uint64_t DecisecondsWaited = 0;
    while (GetReadableBytes() < RequiredBytes)
    {
        if (IsNonBlocking)
        {
            return LINUX_ERR_EAGAIN;
        }

        if (!IsCanonicalModeEnabled() && VTime > 0 && DecisecondsWaited >= static_cast<uint64_t>(VTime))
        {
            break;
        }

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

        if (!IsCanonicalModeEnabled() && VTime > 0)
        {
            ActiveLogicLayer->SleepProcess(CurrentProcess->Id, 1);
            ++DecisecondsWaited;
        }
        else
        {
            ActiveLogicLayer->BlockProcessForTTYInput(CurrentProcess->Id);
        }
    }

    uint64_t AvailableBytes = GetReadableBytes();
    if (!IsCanonicalModeEnabled() && VMin == 0 && VTime > 0 && AvailableBytes == 0)
    {
        return 0;
    }

    char*    OutBuffer   = reinterpret_cast<char*>(Buffer);
    uint64_t BytesCopied = 0;

    while (BytesCopied < Count && GetReadableBytes() > 0)
    {
        OutBuffer[BytesCopied] = KeyboardBuffer[BufferTail];
        BufferTail             = (BufferTail + 1) % KEYBOARD_BUFFER_CAPACITY;
        --BufferedBytes;
        if (IsCanonicalModeEnabled() && CommittedBytes > 0)
        {
            --CommittedBytes;
        }
        ++BytesCopied;

        if (!IsCanonicalModeEnabled() && VMin > 0 && BytesCopied >= static_cast<uint64_t>(VMin))
        {
            break;
        }
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

#ifdef DEBUG_BUILD
    EnsureSerialInitialized();
    int SerialCount = static_cast<int>(Count);
    if (Count > static_cast<uint64_t>(INT32_MAX))
    {
        SerialCount = INT32_MAX;
    }
    SerialWriteBuffer(InBuffer, SerialCount);
#endif

    for (uint64_t Index = 0; Index < Count; ++Index)
    {
        char Character = InBuffer[Index];
        if (OutputAnsiState == AnsiParseState::Normal && Character >= ' ' && Character != 127)
        {
            PutChar(Character);
            continue;
        }

        HandleOutputChar(Character);
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

        LinuxTermios Termios = {
                TermiosInputFlags, TermiosOutputFlags, TermiosControlFlags, TermiosLocalFlags, TermiosLineDiscipline, {},
        };
        memcpy(Termios.ControlCharacters, TermiosControlCharacters, sizeof(Termios.ControlCharacters));
        return Logic->CopyFromKernelToUser(&Termios, reinterpret_cast<void*>(Argument), sizeof(Termios)) ? 0 : LINUX_ERR_EFAULT;
    }

    if (Request == LINUX_IOCTL_TCSETS || Request == LINUX_IOCTL_TCSETSW || Request == LINUX_IOCTL_TCSETSF)
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        LinuxTermios Incoming = {};
        if (!Logic->CopyFromUserToKernel(reinterpret_cast<const void*>(Argument), &Incoming, sizeof(Incoming)))
        {
            return LINUX_ERR_EFAULT;
        }

        bool WasCanonical = IsCanonicalModeEnabled();

        TermiosInputFlags     = Incoming.InputFlags;
        TermiosOutputFlags    = Incoming.OutputFlags;
        TermiosControlFlags   = Incoming.ControlFlags;
        TermiosLocalFlags     = Incoming.LocalFlags;
        TermiosLineDiscipline = Incoming.LineDiscipline;
        memcpy(TermiosControlCharacters, Incoming.ControlCharacters, sizeof(TermiosControlCharacters));

        if (!WasCanonical && IsCanonicalModeEnabled())
        {
            CommittedBytes = 0;
        }

        if (WasCanonical && !IsCanonicalModeEnabled())
        {
            CommittedBytes = BufferedBytes;
            if (CommittedBytes > 0)
            {
                NotifyInputAvailable();
            }
        }

        return 0;
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

        int32_t BytesAvailable = static_cast<int32_t>(GetReadableBytes());
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

    uint8_t EraseCharacter = GetControlCharacter(LINUX_CC_VERASE);
    if (Character == '\b' || static_cast<uint8_t>(Character) == EraseCharacter)
    {
        if (IsCanonicalModeEnabled())
        {
            uint64_t EditableBytes = BufferedBytes - CommittedBytes;
            if (EditableBytes > 0)
            {
                BufferHead = (BufferHead + KEYBOARD_BUFFER_CAPACITY - 1) % KEYBOARD_BUFFER_CAPACITY;
                --BufferedBytes;

                if (IsEchoEnabled())
                {
                    PutChar('\b');
                }
            }
        }

        return 1;
    }

    uint64_t BytesStored = PushKeyboardInput(&Character, 1);
    if (BytesStored == 0)
    {
        return 0;
    }

    if (Character == '\n')
    {
        CommittedBytes = BufferedBytes;
        NotifyInputAvailable();
    }

    if (!IsCanonicalModeEnabled())
    {
        NotifyInputAvailable();
    }

    if (IsEchoEnabled())
    {
        PutChar(Character);
    }

    return BytesStored;
}

uint64_t TTY::GetBufferedInputBytes() const
{
    return GetReadableBytes();
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
