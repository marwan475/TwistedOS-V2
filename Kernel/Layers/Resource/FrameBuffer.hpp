/**
 * File: FrameBuffer.hpp
 * Author: Marwan Mostafa
 * Description: Framebuffer resource metadata declarations.
 */

#pragma once

#include <uefi.hpp>
#include <stdint.h>

class FrameBuffer
{
private:
    uint32_t*                Buffer;
    uint64_t                 BufferSizeBytes;
    uint32_t                 Width;
    uint32_t                 Height;
    uint32_t                 PixelsPerScanLine;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK        PixelInformation;
    bool                     Valid;

public:
    FrameBuffer();

    void Initialize(const EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE& GopMode);
    void Draw(uint32_t X, uint32_t Y, uint32_t RGBA);

    uint32_t*                GetBuffer() const;
    uint64_t                 GetBufferSizeBytes() const;
    uint32_t                 GetWidth() const;
    uint32_t                 GetHeight() const;
    uint32_t                 GetPixelsPerScanLine() const;
    EFI_GRAPHICS_PIXEL_FORMAT GetPixelFormat() const;
    EFI_PIXEL_BITMASK        GetPixelInformation() const;
    bool                     IsValid() const;
};