#include "ELFManager.hpp"

namespace
{
constexpr uint8_t  ELF_MAGIC_0 = 0x7F;
constexpr uint8_t  ELF_MAGIC_1 = 'E';
constexpr uint8_t  ELF_MAGIC_2 = 'L';
constexpr uint8_t  ELF_MAGIC_3 = 'F';
constexpr uint8_t  ELF_CLASS_64 = 2;
constexpr uint8_t  ELF_DATA_LSB = 1;
constexpr uint32_t ELF_VERSION_CURRENT = 1;
constexpr uint32_t ELF_PROGRAM_HEADER_LOAD = 1;
constexpr uint32_t ELF_PROGRAM_HEADER_FLAG_WRITE = 0x2;

bool RangeFitsInImage(uint64_t Offset, uint64_t Size, uint64_t ImageSize)
{
    return Offset <= ImageSize && Size <= (ImageSize - Offset);
}
} // namespace

ELFManager::ELFManager()
{
}

ELFManager::~ELFManager()
{
}

ELFHeader ELFManager::ParseELF(uint64_t PhysicalAddress) const
{
    ELFHeader Header = {};

    if (PhysicalAddress == 0)
    {
        return Header;
    }

    const ELFHeader* RawHeader = reinterpret_cast<const ELFHeader*>(PhysicalAddress);
    Header                     = *RawHeader;

    return Header;
}

bool ELFManager::ValidateELF(const ELFHeader& Header) const
{
    return Header.Magic[0] == ELF_MAGIC_0 && Header.Magic[1] == ELF_MAGIC_1 && Header.Magic[2] == ELF_MAGIC_2 && Header.Magic[3] == ELF_MAGIC_3;
}

bool ELFManager::ValidateELF64(const ELFHeader& Header) const
{
    return ValidateELF(Header) && Header.Class == ELF_CLASS_64 && Header.Data == ELF_DATA_LSB && Header.Version == ELF_VERSION_CURRENT;
}

bool ELFManager::ValidateProgramHeaderTable(const ELFHeader& Header, uint64_t ImageSize) const
{
    if (!ValidateELF64(Header) || Header.ProgramHeaderEntryCount == 0 || Header.ProgramHeaderEntrySize != sizeof(ELFProgramHeader64))
    {
        return false;
    }

    uint64_t ProgramHeaderTableSize = static_cast<uint64_t>(Header.ProgramHeaderEntryCount) * Header.ProgramHeaderEntrySize;
    return RangeFitsInImage(Header.ProgramHeaderOffset, ProgramHeaderTableSize, ImageSize);
}

const ELFProgramHeader64* ELFManager::GetProgramHeaderTable(uint64_t PhysicalAddress, const ELFHeader& Header) const
{
    if (PhysicalAddress == 0)
    {
        return nullptr;
    }

    return reinterpret_cast<const ELFProgramHeader64*>(PhysicalAddress + Header.ProgramHeaderOffset);
}

bool ELFManager::IsLoadableSegment(const ELFProgramHeader64& ProgramHeader) const
{
    return ProgramHeader.Type == ELF_PROGRAM_HEADER_LOAD && ProgramHeader.MemorySize != 0;
}

bool ELFManager::IsWritableSegment(const ELFProgramHeader64& ProgramHeader) const
{
    return (ProgramHeader.Flags & ELF_PROGRAM_HEADER_FLAG_WRITE) != 0;
}

bool ELFManager::ValidateProgramSegment(const ELFProgramHeader64& ProgramHeader, uint64_t ImageSize) const
{
    return ProgramHeader.MemorySize != 0 && ProgramHeader.FileSize <= ProgramHeader.MemorySize && RangeFitsInImage(ProgramHeader.Offset, ProgramHeader.FileSize, ImageSize);
}