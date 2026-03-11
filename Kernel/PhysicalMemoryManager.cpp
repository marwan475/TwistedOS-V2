#include <PhysicalMemoryManager.hpp>

PhysicalMemoryManager::PhysicalMemoryManager(MemoryMapInfo MemoryMap, UINTN NextPageAddress, UINTN CurrentDescriptor,
                                             UINTN RemainingPagesInDescriptor)
    : MemoryMap(MemoryMap), NextPageAddress(NextPageAddress), CurrentDescriptor(CurrentDescriptor),
      RemainingPagesInDescriptor(RemainingPagesInDescriptor)
{
}

void* PhysicalMemoryManager::AllocatePages(UINTN Pages)
{
    if (RemainingPagesInDescriptor < Pages)
    {
        for (UINTN i = CurrentDescriptor + 1; i < MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize; i++)
        {
            EFI_MEMORY_DESCRIPTOR* Desc
                    = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

            if (Desc->Type == EfiConventionalMemory && Desc->NumberOfPages >= Pages)
            {
                CurrentDescriptor          = i;
                RemainingPagesInDescriptor = Desc->NumberOfPages - Pages;
                NextPageAddress            = Desc->PhysicalStart + (Pages * PAGE_SIZE);
                return (void*) Desc->PhysicalStart;
            }
        }

        return NULL;
    }

    RemainingPagesInDescriptor -= Pages;
    void* Page = (void*) NextPageAddress;
    NextPageAddress += Pages * PAGE_SIZE;
    return Page;
}

UINTN PhysicalMemoryManager::GetTotalUsableMemory() const
{
    UINTN TotalUsableMemory = 0;
    UINTN DescriptorCount   = MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize;

    for (UINTN i = 0; i < DescriptorCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR* Desc
                = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        if (Desc->Type == EfiConventionalMemory)
        {
            TotalUsableMemory += Desc->NumberOfPages * PAGE_SIZE;
        }
    }

    return TotalUsableMemory;
}

UINTN PhysicalMemoryManager::GetNextPageAddress() const
{
    return NextPageAddress;
}

UINTN PhysicalMemoryManager::GetCurrentDescriptor() const
{
    return CurrentDescriptor;
}

UINTN PhysicalMemoryManager::GetRemainingPagesInDescriptor() const
{
    return RemainingPagesInDescriptor;
}
