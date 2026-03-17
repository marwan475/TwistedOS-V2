/**
 * File: ELFManager.cpp
 * Author: Marwan Mostafa
 * Description: ELF parsing and loading logic implementation.
 */

#include "ELFManager.hpp"

namespace
{
constexpr uint8_t  ELF_MAGIC_0                   = 0x7F;
constexpr uint8_t  ELF_MAGIC_1                   = 'E';
constexpr uint8_t  ELF_MAGIC_2                   = 'L';
constexpr uint8_t  ELF_MAGIC_3                   = 'F';
constexpr uint8_t  ELF_CLASS_64                  = 2;
constexpr uint8_t  ELF_DATA_LSB                  = 1;
constexpr uint32_t ELF_VERSION_CURRENT           = 1;
constexpr uint32_t ELF_PROGRAM_HEADER_LOAD       = 1;
constexpr uint32_t ELF_PROGRAM_HEADER_FLAG_WRITE = 0x2;

/**
 * Function: RangeFitsInImage
 * Description: Checks whether an offset and size range fits within an ELF image size.
 * Parameters:
 *   uint64_t Offset - Range start offset in the image.
 *   uint64_t Size - Range size in bytes.
 *   uint64_t ImageSize - Total image size in bytes.
 * Returns:
 *   bool - True when range is fully within image bounds.
 */
bool RangeFitsInImage(uint64_t Offset, uint64_t Size, uint64_t ImageSize)
{
    return Offset <= ImageSize && Size <= (ImageSize - Offset);
}
} // namespace

/**
 * Function: ELFManager::ELFManager
 * Description: Constructs an ELF manager.
 * Parameters:
 *   None
 * Returns:
 *   ELFManager - Constructed ELF manager instance.
 */
ELFManager::ELFManager()
{
}

/**
 * Function: ELFManager::~ELFManager
 * Description: Destroys the ELF manager.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
ELFManager::~ELFManager()
{
}

/**
 * Function: ELFManager::ParseELF
 * Description: Reads an ELF header from a physical memory address.
 * Parameters:
 *   uint64_t PhysicalAddress - Address where ELF header begins.
 * Returns:
 *   ELFHeader - Parsed ELF header or zero-initialized header on invalid input.
 */
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

/**
 * Function: ELFManager::ValidateELF
 * Description: Validates ELF magic bytes.
 * Parameters:
 *   const ELFHeader& Header - ELF header to validate.
 * Returns:
 *   bool - True if header contains valid ELF magic.
 */
bool ELFManager::ValidateELF(const ELFHeader& Header) const
{
    return Header.Magic[0] == ELF_MAGIC_0 && Header.Magic[1] == ELF_MAGIC_1 && Header.Magic[2] == ELF_MAGIC_2 && Header.Magic[3] == ELF_MAGIC_3;
}

/**
 * Function: ELFManager::ValidateELF64
 * Description: Validates that the ELF header represents a 64-bit little-endian current-version ELF image.
 * Parameters:
 *   const ELFHeader& Header - ELF header to validate.
 * Returns:
 *   bool - True if header is valid ELF64 image.
 */
bool ELFManager::ValidateELF64(const ELFHeader& Header) const
{
    return ValidateELF(Header) && Header.Class == ELF_CLASS_64 && Header.Data == ELF_DATA_LSB && Header.Version == ELF_VERSION_CURRENT;
}

/**
 * Function: ELFManager::ValidateProgramHeaderTable
 * Description: Validates program header table layout and bounds inside an ELF image.
 * Parameters:
 *   const ELFHeader& Header - ELF header containing program table metadata.
 *   uint64_t ImageSize - ELF image size in bytes.
 * Returns:
 *   bool - True if program header table is valid and in range.
 */
bool ELFManager::ValidateProgramHeaderTable(const ELFHeader& Header, uint64_t ImageSize) const
{
    if (!ValidateELF64(Header) || Header.ProgramHeaderEntryCount == 0 || Header.ProgramHeaderEntrySize != sizeof(ELFProgramHeader64))
    {
        return false;
    }

    uint64_t ProgramHeaderTableSize = static_cast<uint64_t>(Header.ProgramHeaderEntryCount) * Header.ProgramHeaderEntrySize;
    return RangeFitsInImage(Header.ProgramHeaderOffset, ProgramHeaderTableSize, ImageSize);
}

/**
 * Function: ELFManager::GetProgramHeaderTable
 * Description: Returns pointer to ELF64 program header table.
 * Parameters:
 *   uint64_t PhysicalAddress - Base physical address of ELF image.
 *   const ELFHeader& Header - ELF header with table offset.
 * Returns:
 *   const ELFProgramHeader64* - Pointer to program headers or nullptr on invalid address.
 */
const ELFProgramHeader64* ELFManager::GetProgramHeaderTable(uint64_t PhysicalAddress, const ELFHeader& Header) const
{
    if (PhysicalAddress == 0)
    {
        return nullptr;
    }

    return reinterpret_cast<const ELFProgramHeader64*>(PhysicalAddress + Header.ProgramHeaderOffset);
}

/**
 * Function: ELFManager::IsLoadableSegment
 * Description: Checks whether a program header describes a loadable non-empty segment.
 * Parameters:
 *   const ELFProgramHeader64& ProgramHeader - Program header to inspect.
 * Returns:
 *   bool - True if segment is PT_LOAD and has non-zero memory size.
 */
bool ELFManager::IsLoadableSegment(const ELFProgramHeader64& ProgramHeader) const
{
    return ProgramHeader.Type == ELF_PROGRAM_HEADER_LOAD && ProgramHeader.MemorySize != 0;
}

/**
 * Function: ELFManager::IsWritableSegment
 * Description: Checks whether a program header has write permission flag set.
 * Parameters:
 *   const ELFProgramHeader64& ProgramHeader - Program header to inspect.
 * Returns:
 *   bool - True if segment is writable.
 */
bool ELFManager::IsWritableSegment(const ELFProgramHeader64& ProgramHeader) const
{
    return (ProgramHeader.Flags & ELF_PROGRAM_HEADER_FLAG_WRITE) != 0;
}

/**
 * Function: ELFManager::ValidateProgramSegment
 * Description: Validates one program segment size relationship and file-range bounds.
 * Parameters:
 *   const ELFProgramHeader64& ProgramHeader - Segment header to validate.
 *   uint64_t ImageSize - ELF image size in bytes.
 * Returns:
 *   bool - True if segment metadata is valid.
 */
bool ELFManager::ValidateProgramSegment(const ELFProgramHeader64& ProgramHeader, uint64_t ImageSize) const
{
    return ProgramHeader.MemorySize != 0 && ProgramHeader.FileSize <= ProgramHeader.MemorySize && RangeFitsInImage(ProgramHeader.Offset, ProgramHeader.FileSize, ImageSize);
}