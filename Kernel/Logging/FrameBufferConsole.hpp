#pragma once

#include <stdint.h>

class FrameBufferConsole
{
private:
    static constexpr uint32_t FontWidth  = 8;
    static constexpr uint32_t FontHeight = 16;

    uint32_t* framebuffer;
    uint32_t  screenWidth;
    uint32_t  screenHeight;
    uint32_t  pitch;
    uint32_t  cursorX;
    uint32_t  cursorY;
    uint32_t  textColor;

    static FrameBufferConsole* ActiveConsole;

    void DrawChar(int x, int y, char c);

public:
    FrameBufferConsole();

    void Initialize(uint32_t* buffer, uint32_t width, uint32_t height, uint32_t scanline);
    void Clear(uint32_t color = 0x00000000);
    void PutChar(char c);
    int  printf_(const char* format, ...);
    int  dbgprintf_(const char* format, ...);

    static void                SetActive(FrameBufferConsole* console);
    static FrameBufferConsole* GetActive();
};