/**
 * File: FrameBuffer.cpp
 * Author: Marwan Mostafa
 * Description: Framebuffer resource metadata implementation.
 */

#include "FrameBuffer.hpp"

/**
 * Function: FrameBuffer::FrameBuffer
 * Description: Constructs an empty framebuffer resource descriptor.
 * Parameters:
 *   None
 * Returns:
 *   FrameBuffer - Constructed framebuffer descriptor.
 */
FrameBuffer::FrameBuffer()
    : Buffer(nullptr), BufferSizeBytes(0), Width(0), Height(0), PixelsPerScanLine(0), PixelFormat(PixelFormatMax), PixelInformation({0, 0, 0, 0}), Valid(false)
{
}

/**
 * Function: FrameBuffer::Initialize
 * Description: Stores framebuffer details from UEFI GOP mode metadata.
 * Parameters:
 *   const EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE& GopMode - GOP mode snapshot provided at boot.
 * Returns:
 *   void - No return value.
 */
void FrameBuffer::Initialize(const EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE& GopMode)
{
    Buffer            = nullptr;
    BufferSizeBytes   = 0;
    Width             = 0;
    Height            = 0;
    PixelsPerScanLine = 0;
    PixelFormat       = PixelFormatMax;
    PixelInformation  = {0, 0, 0, 0};
    Valid             = false;

    if (GopMode.Info == nullptr || GopMode.FrameBufferBase == 0)
    {
        return;
    }

    if (GopMode.Info->HorizontalResolution == 0 || GopMode.Info->VerticalResolution == 0 || GopMode.Info->PixelsPerScanLine == 0)
    {
        return;
    }

    Buffer            = (uint32_t*) (uint64_t) GopMode.FrameBufferBase;
    BufferSizeBytes   = (uint64_t) GopMode.FrameBufferSize;
    Width             = GopMode.Info->HorizontalResolution;
    Height            = GopMode.Info->VerticalResolution;
    PixelsPerScanLine = GopMode.Info->PixelsPerScanLine;
    PixelFormat       = GopMode.Info->PixelFormat;
    PixelInformation  = GopMode.Info->PixelInformation;
    Valid             = true;
}

void FrameBuffer::Draw(uint32_t X, uint32_t Y, uint32_t RGBA)
{
    if (!Valid || Buffer == nullptr)
    {
        return;
    }

    if (X >= Width || Y >= Height)
    {
        return;
    }

    uint64_t PixelIndex = static_cast<uint64_t>(Y) * static_cast<uint64_t>(PixelsPerScanLine) + static_cast<uint64_t>(X);
    uint64_t ByteOffset = PixelIndex * sizeof(uint32_t);

    if (ByteOffset + sizeof(uint32_t) > BufferSizeBytes)
    {
        return;
    }

    Buffer[PixelIndex] = RGBA;
}

uint32_t* FrameBuffer::GetBuffer() const
{
    return Buffer;
}

uint64_t FrameBuffer::GetBufferSizeBytes() const
{
    return BufferSizeBytes;
}

uint32_t FrameBuffer::GetWidth() const
{
    return Width;
}

uint32_t FrameBuffer::GetHeight() const
{
    return Height;
}

uint32_t FrameBuffer::GetPixelsPerScanLine() const
{
    return PixelsPerScanLine;
}

EFI_GRAPHICS_PIXEL_FORMAT FrameBuffer::GetPixelFormat() const
{
    return PixelFormat;
}

EFI_PIXEL_BITMASK FrameBuffer::GetPixelInformation() const
{
    return PixelInformation;
}

bool FrameBuffer::IsValid() const
{
    return Valid;
}