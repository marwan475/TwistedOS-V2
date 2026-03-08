#include <MemoryManager.hpp>

MemoryManager::MemoryManager(MemoryMapInfo MemoryMap, Console* efiConsole) : MemoryMap(MemoryMap) , efiConsole(efiConsole)
{
}

MemoryManager::~MemoryManager()
{
}

void* MemoryManager::AllocateAvailablePagesFromMemoryMap(UINTN Pages)
{
    static void* NextPageAddress            = NULL;
    static UINTN CurrentDescriptor          = 0;
    static UINTN RemainingPagesInDescriptor = 0;

    if (RemainingPagesInDescriptor < Pages)
    {
        for (UINTN i = CurrentDescriptor + 1; i < MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize; i++)
        {
            EFI_MEMORY_DESCRIPTOR* desc
                    = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

            if (desc->Type == EfiConventionalMemory && desc->NumberOfPages >= Pages)
            {
                CurrentDescriptor          = i;
                RemainingPagesInDescriptor = desc->NumberOfPages - Pages;
                NextPageAddress            = (void*) (desc->PhysicalStart + (Pages * PAGE_SIZE));
                return (void*) desc->PhysicalStart;
            }

            if (i >= MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize)
            {
                return NULL;
            }
        }
    }

    RemainingPagesInDescriptor -= Pages;
    void* Page      = NextPageAddress;
    NextPageAddress = (void*) ((UINT8*) Page + (Pages * PAGE_SIZE));
    return Page;
}

bool MapPage(UINTN PysicalAddr, UINTN VirtualAddr)
{
    return false;
}
