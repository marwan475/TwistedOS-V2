/**
 * File: TTY.hpp
 * Author: Marwan Mostafa
 * Description: TTY device abstraction with keyboard input buffer and framebuffer-backed output.
 */

#pragma once

#include <stdint.h>

#include <Logging/FrameBufferConsole.hpp>

struct File;
struct FileOperations;

class TTY
{
private:
    static constexpr uint64_t KEYBOARD_BUFFER_CAPACITY = 1024;

    FrameBufferConsole* Console;
    char                KeyboardBuffer[KEYBOARD_BUFFER_CAPACITY];
    uint64_t            BufferHead;
    uint64_t            BufferTail;
    uint64_t            BufferedBytes;

    static FileOperations TerminalFileOperations;

public:
    explicit TTY(FrameBufferConsole* Console);

    int64_t Read(File* OpenFile, void* Buffer, uint64_t Count);
    int64_t Write(File* OpenFile, const void* Buffer, uint64_t Count);

    uint64_t PushKeyboardInput(const char* Buffer, uint64_t Count);
    uint64_t PushKeyboardInputChar(char Character);

    FileOperations* GetFileOperations();

    static int64_t ReadFileOperation(File* OpenFile, void* Buffer, uint64_t Count);
    static int64_t WriteFileOperation(File* OpenFile, const void* Buffer, uint64_t Count);
};
