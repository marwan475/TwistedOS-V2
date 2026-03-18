/**
 * File: FrameBuffer.cpp
 * Author: Marwan Mostafa
 * Description: Framebuffer resource metadata implementation.
 */

#include "FrameBuffer.hpp"

#include <Layers/Dispatcher.hpp>
#include <Layers/Logic/VirtualFileSystem.hpp>
#include <Layers/Resource/VirtualAddressSpace.hpp>
#include <Memory/VirtualMemoryManager.hpp>
#include <CommonUtils.hpp>

namespace
{
constexpr int64_t LINUX_ERR_EFAULT = -14;
constexpr int64_t LINUX_ERR_EINVAL = -22;
constexpr int64_t LINUX_ERR_ENODEV = -19;

constexpr int32_t LINUX_SEEK_SET = 0;
constexpr int32_t LINUX_SEEK_CUR = 1;
constexpr int32_t LINUX_SEEK_END = 2;

constexpr uint64_t USER_FRAMEBUFFER_MMAP_BASE = 0x0000700000000000;
constexpr uint64_t PAGE_SIZE_BYTES            = 4096;
constexpr uint64_t PAGE_ALIGNMENT_MASK        = PAGE_SIZE_BYTES - 1;
} // namespace

FileOperations FrameBuffer::FrameBufferFileOperations = {
    nullptr,
    &FrameBuffer::WriteFileOperation,
    &FrameBuffer::SeekFileOperation,
    &FrameBuffer::MemoryMapFileOperation,
};

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

int64_t FrameBuffer::Write(File* OpenFile, const void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || (Buffer == nullptr && Count != 0))
    {
        return LINUX_ERR_EFAULT;
    }

    if (!Valid || this->Buffer == nullptr)
    {
        return LINUX_ERR_ENODEV;
    }

    if (Count == 0)
    {
        return 0;
    }

    if (OpenFile->CurrentOffset >= BufferSizeBytes)
    {
        return 0;
    }

    uint64_t RemainingBytes = BufferSizeBytes - OpenFile->CurrentOffset;
    uint64_t BytesToWrite   = (Count < RemainingBytes) ? Count : RemainingBytes;

    uint8_t* Destination = reinterpret_cast<uint8_t*>(this->Buffer) + OpenFile->CurrentOffset;
    memcpy(Destination, Buffer, static_cast<size_t>(BytesToWrite));
    OpenFile->CurrentOffset += BytesToWrite;

    return static_cast<int64_t>(BytesToWrite);
}

int64_t FrameBuffer::Seek(File* OpenFile, int64_t Offset, int32_t Whence)
{
    if (OpenFile == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    int64_t Base = 0;

    if (Whence == LINUX_SEEK_SET)
    {
        Base = 0;
    }
    else if (Whence == LINUX_SEEK_CUR)
    {
        Base = static_cast<int64_t>(OpenFile->CurrentOffset);
    }
    else if (Whence == LINUX_SEEK_END)
    {
        Base = static_cast<int64_t>(BufferSizeBytes);
    }
    else
    {
        return LINUX_ERR_EINVAL;
    }

    int64_t NewOffsetSigned = Base + Offset;
    if (NewOffsetSigned < 0)
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t NewOffset = static_cast<uint64_t>(NewOffsetSigned);
    if (NewOffset > BufferSizeBytes)
    {
        return LINUX_ERR_EINVAL;
    }

    OpenFile->CurrentOffset = NewOffset;
    return static_cast<int64_t>(NewOffset);
}

int64_t FrameBuffer::MemoryMap(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address)
{
    if (OpenFile == nullptr || Address == nullptr || AddressSpace == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (!Valid || this->Buffer == nullptr)
    {
        return LINUX_ERR_ENODEV;
    }

    if (Offset > BufferSizeBytes)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Length > (BufferSizeBytes - Offset))
    {
        return LINUX_ERR_EINVAL;
    }

    uint64_t ProcessPageMapL4TableAddr = AddressSpace->GetPageMapL4TableAddr();
    if (ProcessPageMapL4TableAddr == 0)
    {
        return LINUX_ERR_EFAULT;
    }

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr || ActiveDispatcher->GetResourceLayer() == nullptr || ActiveDispatcher->GetResourceLayer()->GetPMM() == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    uint64_t PhysicalStartAddress = reinterpret_cast<uint64_t>(this->Buffer) + Offset;
    uint64_t PhysicalBaseAddress  = PhysicalStartAddress & ~PAGE_ALIGNMENT_MASK;
    uint64_t InPageOffset         = PhysicalStartAddress & PAGE_ALIGNMENT_MASK;
    uint64_t MappingSizeBytes     = InPageOffset + Length;
    uint64_t RequiredPages        = (MappingSizeBytes + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES;

    uint64_t VirtualBaseAddress = (USER_FRAMEBUFFER_MMAP_BASE + Offset) & ~PAGE_ALIGNMENT_MASK;

    VirtualMemoryManager UserVMM(ProcessPageMapL4TableAddr, *ActiveDispatcher->GetResourceLayer()->GetPMM());

    for (uint64_t PageIndex = 0; PageIndex < RequiredPages; ++PageIndex)
    {
        uint64_t PhysicalPageAddress = PhysicalBaseAddress + (PageIndex * PAGE_SIZE_BYTES);
        uint64_t VirtualPageAddress  = VirtualBaseAddress + (PageIndex * PAGE_SIZE_BYTES);

        if (!UserVMM.MapPage(PhysicalPageAddress, VirtualPageAddress, PageMappingFlags(true, true)))
        {
            return LINUX_ERR_EFAULT;
        }
    }

    *Address = VirtualBaseAddress + InPageOffset;
    return static_cast<int64_t>(Length);
}

FileOperations* FrameBuffer::GetFileOperations()
{
    return &FrameBufferFileOperations;
}

int64_t FrameBuffer::WriteFileOperation(File* OpenFile, const void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    FrameBuffer* FB = reinterpret_cast<FrameBuffer*>(OpenFile->Node->NodeData);
    if (FB == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    return FB->Write(OpenFile, Buffer, Count);
}

int64_t FrameBuffer::SeekFileOperation(File* OpenFile, int64_t Offset, int32_t Whence)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    FrameBuffer* FB = reinterpret_cast<FrameBuffer*>(OpenFile->Node->NodeData);
    if (FB == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    return FB->Seek(OpenFile, Offset, Whence);
}

int64_t FrameBuffer::MemoryMapFileOperation(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    FrameBuffer* FB = reinterpret_cast<FrameBuffer*>(OpenFile->Node->NodeData);
    if (FB == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    return FB->MemoryMap(OpenFile, Length, Offset, AddressSpace, Address);
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