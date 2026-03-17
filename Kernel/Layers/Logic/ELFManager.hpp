#pragma once

#include <stdint.h>

struct __attribute__((packed)) ELFHeader
{
    uint8_t  Magic[4];
    uint8_t  Class;
    uint8_t  Data;
    uint8_t  IdentifierVersion;
    uint8_t  OSABI;
    uint8_t  ABIVersion;
    uint8_t  Padding[7];
    uint16_t Type;
    uint16_t Machine;
    uint32_t Version;
    uint64_t Entry;
    uint64_t ProgramHeaderOffset;
    uint64_t SectionHeaderOffset;
    uint32_t Flags;
    uint16_t HeaderSize;
    uint16_t ProgramHeaderEntrySize;
    uint16_t ProgramHeaderEntryCount;
    uint16_t SectionHeaderEntrySize;
    uint16_t SectionHeaderEntryCount;
    uint16_t SectionHeaderNameIndex;
};

static_assert(sizeof(ELFHeader) == 64, "ELFHeader must match the 64-bit ELF header layout");

class ELFManager
{
public:
    ELFManager();
    ~ELFManager();

    ELFHeader ParseELF(uint64_t PhysicalAddress) const;
};