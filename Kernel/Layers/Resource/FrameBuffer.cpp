/**
 * File: FrameBuffer.cpp
 * Author: Marwan Mostafa
 * Description: Framebuffer resource metadata implementation.
 */

#include "FrameBuffer.hpp"

#include <CommonUtils.hpp>
#include <Layers/Dispatcher.hpp>
#include <Layers/Logic/LogicLayer.hpp>
#include <Layers/Logic/VirtualFileSystem.hpp>
#include <Layers/Resource/VirtualAddressSpace.hpp>
#include <Memory/VirtualMemoryManager.hpp>

namespace
{
constexpr int64_t LINUX_ERR_EFAULT = -14;
constexpr int64_t LINUX_ERR_EINVAL = -22;
constexpr int64_t LINUX_ERR_ENODEV = -19;
constexpr int64_t LINUX_ERR_ENOTTY = -25;

constexpr int32_t LINUX_SEEK_SET = 0;
constexpr int32_t LINUX_SEEK_CUR = 1;
constexpr int32_t LINUX_SEEK_END = 2;

constexpr uint64_t USER_FRAMEBUFFER_MMAP_BASE = 0x0000700000000000;
constexpr uint64_t PAGE_SIZE_BYTES            = 4096;
constexpr uint64_t PAGE_ALIGNMENT_MASK        = PAGE_SIZE_BYTES - 1;

constexpr uint64_t LINUX_IOCTL_FBIOGET_VSCREENINFO = 0x4600;
constexpr uint64_t LINUX_IOCTL_FBIOPUT_VSCREENINFO = 0x4601;
constexpr uint64_t LINUX_IOCTL_FBIOGET_FSCREENINFO = 0x4602;
constexpr uint64_t LINUX_IOCTL_FBIOGETCMAP         = 0x4604;
constexpr uint64_t LINUX_IOCTL_FBIOPUTCMAP         = 0x4605;
constexpr uint64_t LINUX_IOCTL_FBIOPAN_DISPLAY     = 0x4606;
constexpr uint64_t LINUX_IOCTL_FBIOBLANK           = 0x4611;

constexpr uint32_t LINUX_FB_TYPE_PACKED_PIXELS = 0;
constexpr uint32_t LINUX_FB_VISUAL_TRUECOLOR   = 2;
constexpr uint32_t LINUX_FB_ACCEL_NONE         = 0;

struct LinuxFBBitfield
{
    uint32_t Offset;
    uint32_t Length;
    uint32_t MostSignificantBitRight;
};

struct LinuxFBFixScreenInfo
{
    char          Identifier[16];
    unsigned long ScreenMemoryStart;
    uint32_t      ScreenMemoryLength;
    uint32_t      Type;
    uint32_t      TypeAuxiliary;
    uint32_t      Visual;
    uint16_t      XPanStep;
    uint16_t      YPanStep;
    uint16_t      YWrapStep;
    uint32_t      LineLength;
    unsigned long MMIOStart;
    uint32_t      MMIOLength;
    uint32_t      Acceleration;
    uint16_t      Capabilities;
    uint16_t      Reserved[2];
};

struct LinuxFBVariableScreenInfo
{
    uint32_t       XResolution;
    uint32_t       YResolution;
    uint32_t       XResolutionVirtual;
    uint32_t       YResolutionVirtual;
    uint32_t       XOffset;
    uint32_t       YOffset;
    uint32_t       BitsPerPixel;
    uint32_t       Grayscale;
    LinuxFBBitfield Red;
    LinuxFBBitfield Green;
    LinuxFBBitfield Blue;
    LinuxFBBitfield Transparency;
    uint32_t       NonStandardFormat;
    uint32_t       Activate;
    uint32_t       PhysicalHeightMillimeters;
    uint32_t       PhysicalWidthMillimeters;
    uint32_t       AccelerationFlags;
    uint32_t       PixelClockPicoseconds;
    uint32_t       LeftMargin;
    uint32_t       RightMargin;
    uint32_t       UpperMargin;
    uint32_t       LowerMargin;
    uint32_t       HorizontalSyncLength;
    uint32_t       VerticalSyncLength;
    uint32_t       SyncFlags;
    uint32_t       VideoMode;
    uint32_t       Rotation;
    uint32_t       Colorspace;
    uint32_t       Reserved[4];
};

uint32_t CountSetBits(uint32_t Value)
{
    uint32_t Count = 0;
    while (Value != 0)
    {
        Count += (Value & 1U);
        Value >>= 1U;
    }

    return Count;
}

uint32_t CountTrailingZeros(uint32_t Value)
{
    if (Value == 0)
    {
        return 0;
    }

    uint32_t Shift = 0;
    while ((Value & 1U) == 0)
    {
        Value >>= 1U;
        ++Shift;
    }

    return Shift;
}

LinuxFBBitfield MakeBitfieldFromMask(uint32_t Mask)
{
    LinuxFBBitfield Field = {};
    if (Mask == 0)
    {
        return Field;
    }

    Field.Offset = CountTrailingZeros(Mask);
    Field.Length = CountSetBits(Mask);
    return Field;
}

void PopulatePixelBitfields(EFI_GRAPHICS_PIXEL_FORMAT Format, const EFI_PIXEL_BITMASK& PixelInformation, LinuxFBBitfield* Red, LinuxFBBitfield* Green, LinuxFBBitfield* Blue,
                            LinuxFBBitfield* Transparency)
{
    if (Red == nullptr || Green == nullptr || Blue == nullptr || Transparency == nullptr)
    {
        return;
    }

    *Red          = {};
    *Green        = {};
    *Blue         = {};
    *Transparency = {};

    if (Format == PixelRedGreenBlueReserved8BitPerColor)
    {
        Red->Offset          = 0;
        Red->Length          = 8;
        Green->Offset        = 8;
        Green->Length        = 8;
        Blue->Offset         = 16;
        Blue->Length         = 8;
        Transparency->Offset = 24;
        Transparency->Length = 8;
        return;
    }

    if (Format == PixelBlueGreenRedReserved8BitPerColor)
    {
        Blue->Offset         = 0;
        Blue->Length         = 8;
        Green->Offset        = 8;
        Green->Length        = 8;
        Red->Offset          = 16;
        Red->Length          = 8;
        Transparency->Offset = 24;
        Transparency->Length = 8;
        return;
    }

    if (Format == PixelBitMask)
    {
        *Red          = MakeBitfieldFromMask(PixelInformation.RedMask);
        *Green        = MakeBitfieldFromMask(PixelInformation.GreenMask);
        *Blue         = MakeBitfieldFromMask(PixelInformation.BlueMask);
        *Transparency = MakeBitfieldFromMask(PixelInformation.ReservedMask);
    }
}

void PopulateVariableScreenInfo(const FrameBuffer* FB, LinuxFBVariableScreenInfo* Info)
{
    if (FB == nullptr || Info == nullptr)
    {
        return;
    }

    *Info                            = {};
    Info->XResolution                = FB->GetWidth();
    Info->YResolution                = FB->GetHeight();
    Info->XResolutionVirtual         = FB->GetPixelsPerScanLine();
    Info->YResolutionVirtual         = FB->GetHeight();
    Info->XOffset                    = 0;
    Info->YOffset                    = 0;
    Info->BitsPerPixel               = 32;
    Info->Grayscale                  = 0;
    Info->PhysicalHeightMillimeters  = 0xFFFFFFFFu;
    Info->PhysicalWidthMillimeters   = 0xFFFFFFFFu;
    Info->AccelerationFlags          = 0;
    Info->PixelClockPicoseconds      = 0;
    Info->LeftMargin                 = 0;
    Info->RightMargin                = 0;
    Info->UpperMargin                = 0;
    Info->LowerMargin                = 0;
    Info->HorizontalSyncLength       = 0;
    Info->VerticalSyncLength         = 0;
    Info->SyncFlags                  = 0;
    Info->VideoMode                  = 0;
    Info->Rotation                   = 0;
    Info->Colorspace                 = 0;

    PopulatePixelBitfields(FB->GetPixelFormat(), FB->GetPixelInformation(), &Info->Red, &Info->Green, &Info->Blue, &Info->Transparency);
}

void PopulateFixedScreenInfo(const FrameBuffer* FB, LinuxFBFixScreenInfo* Info)
{
    if (FB == nullptr || Info == nullptr)
    {
        return;
    }

    *Info = {};
    memcpy(Info->Identifier, "TwistedOS-fb0", 12);

    Info->ScreenMemoryStart = reinterpret_cast<unsigned long>(FB->GetBuffer());
    Info->ScreenMemoryLength = (FB->GetBufferSizeBytes() > 0xFFFFFFFFu) ? 0xFFFFFFFFu : static_cast<uint32_t>(FB->GetBufferSizeBytes());
    Info->Type              = LINUX_FB_TYPE_PACKED_PIXELS;
    Info->TypeAuxiliary     = 0;
    Info->Visual            = LINUX_FB_VISUAL_TRUECOLOR;
    Info->XPanStep          = 0;
    Info->YPanStep          = 0;
    Info->YWrapStep         = 0;
    Info->LineLength        = FB->GetPixelsPerScanLine() * sizeof(uint32_t);
    Info->MMIOStart         = 0;
    Info->MMIOLength        = 0;
    Info->Acceleration      = LINUX_FB_ACCEL_NONE;
    Info->Capabilities      = 0;
}
} // namespace

FileOperations FrameBuffer::FrameBufferFileOperations = {
        nullptr, &FrameBuffer::WriteFileOperation, &FrameBuffer::SeekFileOperation, &FrameBuffer::MemoryMapFileOperation, &FrameBuffer::IoctlFileOperation,
};

/**
 * Function: FrameBuffer::FrameBuffer
 * Description: Constructs an empty framebuffer resource descriptor.
 * Parameters:
 *   None
 * Returns:
 *   FrameBuffer - Constructed framebuffer descriptor.
 */
FrameBuffer::FrameBuffer() : Buffer(nullptr), BufferSizeBytes(0), Width(0), Height(0), PixelsPerScanLine(0), PixelFormat(PixelFormatMax), PixelInformation({0, 0, 0, 0}), Valid(false)
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

    if (OpenFile->Node != nullptr)
    {
        OpenFile->Node->NodeSize = BufferSizeBytes;
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

    if (OpenFile->Node != nullptr)
    {
        OpenFile->Node->NodeSize = BufferSizeBytes;
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

    if (OpenFile->Node != nullptr)
    {
        OpenFile->Node->NodeSize = BufferSizeBytes;
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

int64_t FrameBuffer::Ioctl(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic, Process* RunningProcess)
{
    (void) RunningProcess;

    if (OpenFile == nullptr || OpenFile->Node == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    if (!Valid || Buffer == nullptr)
    {
        return LINUX_ERR_ENODEV;
    }

    OpenFile->Node->NodeSize = BufferSizeBytes;

    if (Request == LINUX_IOCTL_FBIOGET_VSCREENINFO)
    {
        if (Argument == 0 || Logic == nullptr)
        {
            return LINUX_ERR_EFAULT;
        }

        LinuxFBVariableScreenInfo VariableInfo = {};
        PopulateVariableScreenInfo(this, &VariableInfo);
        return Logic->CopyFromKernelToUser(&VariableInfo, reinterpret_cast<void*>(Argument), sizeof(VariableInfo)) ? 0 : LINUX_ERR_EFAULT;
    }

    if (Request == LINUX_IOCTL_FBIOGET_FSCREENINFO)
    {
        if (Argument == 0 || Logic == nullptr)
        {
            return LINUX_ERR_EFAULT;
        }

        LinuxFBFixScreenInfo FixedInfo = {};
        PopulateFixedScreenInfo(this, &FixedInfo);
        return Logic->CopyFromKernelToUser(&FixedInfo, reinterpret_cast<void*>(Argument), sizeof(FixedInfo)) ? 0 : LINUX_ERR_EFAULT;
    }

    if (Request == LINUX_IOCTL_FBIOPUT_VSCREENINFO)
    {
        if (Argument == 0 || Logic == nullptr)
        {
            return LINUX_ERR_EFAULT;
        }

        LinuxFBVariableScreenInfo RequestedInfo = {};
        if (!Logic->CopyFromUserToKernel(reinterpret_cast<const void*>(Argument), &RequestedInfo, sizeof(RequestedInfo)))
        {
            return LINUX_ERR_EFAULT;
        }

        if (RequestedInfo.XResolution != Width || RequestedInfo.YResolution != Height || RequestedInfo.XOffset != 0 || RequestedInfo.YOffset != 0 || RequestedInfo.BitsPerPixel != 32)
        {
            return LINUX_ERR_EINVAL;
        }

        return 0;
    }

    if (Request == LINUX_IOCTL_FBIOPAN_DISPLAY)
    {
        if (Argument == 0 || Logic == nullptr)
        {
            return LINUX_ERR_EFAULT;
        }

        LinuxFBVariableScreenInfo RequestedPanInfo = {};
        if (!Logic->CopyFromUserToKernel(reinterpret_cast<const void*>(Argument), &RequestedPanInfo, sizeof(RequestedPanInfo)))
        {
            return LINUX_ERR_EFAULT;
        }

        if (RequestedPanInfo.XOffset != 0 || RequestedPanInfo.YOffset != 0)
        {
            return LINUX_ERR_EINVAL;
        }

        return 0;
    }

    if (Request == LINUX_IOCTL_FBIOBLANK)
    {
        return 0;
    }

    if (Request == LINUX_IOCTL_FBIOGETCMAP || Request == LINUX_IOCTL_FBIOPUTCMAP)
    {
        return LINUX_ERR_EINVAL;
    }

    (void) RunningProcess;
    return LINUX_ERR_ENOTTY;
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

int64_t FrameBuffer::IoctlFileOperation(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic, Process* RunningProcess)
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

    return FB->Ioctl(OpenFile, Request, Argument, Logic, RunningProcess);
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