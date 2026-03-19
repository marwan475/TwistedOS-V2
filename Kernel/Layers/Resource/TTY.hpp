/**
 * File: TTY.hpp
 * Author: Marwan Mostafa
 * Description: TTY device abstraction with keyboard input buffer and framebuffer-backed output.
 */

#pragma once

#include "FrameBuffer.hpp"

#include <stdint.h>

struct File;
struct FileOperations;
class LogicLayer;

class TTY
{
private:
    static constexpr uint32_t FONT_WIDTH               = 8;
    static constexpr uint32_t FONT_HEIGHT              = 16;
    static constexpr uint64_t KEYBOARD_BUFFER_CAPACITY = 1024;

    FrameBuffer* FrameBufferDevice;
    uint32_t     CursorX;
    uint32_t     CursorY;
    uint32_t     TextColor;
    uint32_t     BackgroundColor;
    char         KeyboardBuffer[KEYBOARD_BUFFER_CAPACITY];
    uint64_t     BufferHead;
    uint64_t     BufferTail;
    uint64_t     BufferedBytes;

    void ClearScreen();
    void DrawChar(uint32_t X, uint32_t Y, char Character);
    void PutChar(char Character);

    static FileOperations TerminalFileOperations;

public:
    explicit TTY(FrameBuffer* FrameBuffer, uint32_t InitialCursorX = 0, uint32_t InitialCursorY = 0);

    int64_t Read(File* OpenFile, void* Buffer, uint64_t Count);
    int64_t Write(File* OpenFile, const void* Buffer, uint64_t Count);
    int64_t Ioctl(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic);
    int     printf_(const char* Format, ...);

    uint64_t PushKeyboardInput(const char* Buffer, uint64_t Count);
    uint64_t PushKeyboardInputChar(char Character);

    FileOperations* GetFileOperations();

    static int64_t ReadFileOperation(File* OpenFile, void* Buffer, uint64_t Count);
    static int64_t WriteFileOperation(File* OpenFile, const void* Buffer, uint64_t Count);
    static int64_t SeekFileOperation(File* OpenFile, int64_t Offset, int32_t Whence);
    static int64_t MemoryMapFileOperation(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address);
    static int64_t IoctlFileOperation(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic);
};
