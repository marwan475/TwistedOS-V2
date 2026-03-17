/**
 * File: TTY.cpp
 * Author: Marwan Mostafa
 * Description: TTY device implementation for buffered keyboard input and console output.
 */

#include "TTY.hpp"

#include <Layers/Logic/VirtualFileSystem.hpp>

namespace
{
constexpr int64_t LINUX_ERR_EFAULT = -14;
constexpr int64_t LINUX_ERR_EINVAL = -22;
constexpr int64_t LINUX_ERR_ENODEV = -19;
constexpr int64_t LINUX_ERR_ENOSPC = -28;
} // namespace

FileOperations TTY::TerminalFileOperations = {
    &TTY::ReadFileOperation,
    &TTY::WriteFileOperation,
};

TTY::TTY(FrameBufferConsole* Console) : Console(Console), KeyboardBuffer{}, BufferHead(0), BufferTail(0), BufferedBytes(0)
{
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

    if (Console == nullptr)
    {
        return LINUX_ERR_ENODEV;
    }

    const char* InBuffer = reinterpret_cast<const char*>(Buffer);
    for (uint64_t Index = 0; Index < Count; ++Index)
    {
        Console->PutChar(InBuffer[Index]);
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
