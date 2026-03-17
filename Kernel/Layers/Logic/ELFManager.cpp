#include "ELFManager.hpp"

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