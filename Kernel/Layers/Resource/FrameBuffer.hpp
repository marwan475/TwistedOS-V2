/**
 * File: FrameBuffer.hpp
 * Author: Marwan Mostafa
 * Description: Framebuffer resource metadata declarations.
 */

#pragma once

#include <stdint.h>
#include <uefi.hpp>

class VirtualAddressSpace;
class LogicLayer;
struct Process;

struct File;
struct FileOperations;

class FrameBuffer
{
private:
    uint32_t*                 Buffer;
    uint64_t                  BufferSizeBytes;
    uint32_t                  Width;
    uint32_t                  Height;
    uint32_t                  PixelsPerScanLine;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK         PixelInformation;
    bool                      Valid;

    static FileOperations FrameBufferFileOperations;

public:
    FrameBuffer();

    void    Initialize(const EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE& GopMode);
    void    Draw(uint32_t X, uint32_t Y, uint32_t RGBA);
    int64_t Write(File* OpenFile, const void* Buffer, uint64_t Count);
    int64_t Seek(File* OpenFile, int64_t Offset, int32_t Whence);
    int64_t MemoryMap(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address);
    int64_t Ioctl(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic, Process* RunningProcess);

    FileOperations* GetFileOperations();

    static int64_t WriteFileOperation(File* OpenFile, const void* Buffer, uint64_t Count);
    static int64_t SeekFileOperation(File* OpenFile, int64_t Offset, int32_t Whence);
    static int64_t MemoryMapFileOperation(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address);
    static int64_t IoctlFileOperation(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic, Process* RunningProcess);

    uint32_t*                 GetBuffer() const;
    uint64_t                  GetBufferSizeBytes() const;
    uint32_t                  GetWidth() const;
    uint32_t                  GetHeight() const;
    uint32_t                  GetPixelsPerScanLine() const;
    EFI_GRAPHICS_PIXEL_FORMAT GetPixelFormat() const;
    EFI_PIXEL_BITMASK         GetPixelInformation() const;
    bool                      IsValid() const;
};