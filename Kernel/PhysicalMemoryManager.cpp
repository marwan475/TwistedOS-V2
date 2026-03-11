#include <CommonUtils.hpp>
#include <Logging/FrameBufferConsole.hpp>
#include <PhysicalMemoryManager.hpp>

PhysicalMemoryManager::PhysicalMemoryManager(MemoryMapInfo MemoryMap, UINTN NextPageAddress, UINTN CurrentDescriptor,
                                             UINTN RemainingPagesInDescriptor)
    : MemoryMap(MemoryMap), NextPageAddress(NextPageAddress), CurrentDescriptor(CurrentDescriptor),
      RemainingPagesInDescriptor(RemainingPagesInDescriptor), TotalUsableMemory(0), TotalNumberOfPages(0)
{
    InitializeMemoryStats();
}

void PhysicalMemoryManager::InitializeMemoryStats()
{
    UINTN DescriptorCount = MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize;

    for (UINTN i = 0; i < DescriptorCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR* Desc
                = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        if (Desc->Type == EfiConventionalMemory)
        {
            TotalUsableMemory += Desc->NumberOfPages * PAGE_SIZE;
            TotalNumberOfPages += Desc->NumberOfPages;
        }
    }
}

void* PhysicalMemoryManager::AllocatePagesFromMemoryMap(UINTN Pages)
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

UINTN PhysicalMemoryManager::TotalUsableMemoryBytes() const
{
    return TotalUsableMemory;
}

UINTN PhysicalMemoryManager::TotalPages() const
{
    return TotalNumberOfPages;
}

void PhysicalMemoryManager::PrintConventionalMemoryMap(FrameBufferConsole& Console) const
{
    UINTN DescriptorCount             = MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize;
    UINTN ConventionalDescriptorCount = 0;

    for (UINTN i = 0; i < DescriptorCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR* Desc
                = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        if (Desc->Type == EfiConventionalMemory)
        {
            ConventionalDescriptorCount++;
        }
    }

    Console.printf_("Conventional memory descriptors: %llu\n", (unsigned long long) ConventionalDescriptorCount);

    UINTN ConventionalDescriptorIndex = 0;

    for (UINTN i = 0; i < DescriptorCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR* Desc
                = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        if (Desc->Type != EfiConventionalMemory)
        {
            continue;
        }

        Console.printf_("Descriptor %llu: %llu pages\n", (unsigned long long) ConventionalDescriptorIndex,
                        (unsigned long long) Desc->NumberOfPages);
        ConventionalDescriptorIndex++;
    }
}

void PhysicalMemoryManager::PrintMemoryDescriptors(FrameBufferConsole& Console) const
{
    uint64_t DescriptorCount = MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize;

    for (uint64_t i = 0; i < DescriptorCount; i++)
    {
        MemoryDescriptor* Md = &MemoryDescriptors[i];

        if (Md->TotalNumberOfPages == 0)
            continue;

        Console.printf_("Descriptor %llu: base=0x%llx total=%llu free=%llu bitmap=%s\n", (unsigned long long) i,
                        (unsigned long long) Md->PhysicalAddressStart, (unsigned long long) Md->TotalNumberOfPages,
                        (unsigned long long) Md->NumberOfFreePages, Md->BitMap ? "yes" : "no");
    }
}

void* InitializeMemoryDescriptorBitMap(MemoryDescriptor* Md)
{
    uint64_t BitMapSize = (Md->NumberOfFreePages) / 8;

    // Allocate BitMap From the descriptor itself
    uint64_t PagesForBitMap = ((BitMapSize + PAGE_SIZE - 1) / PAGE_SIZE) + 1;
    void*    BitMapAddr     = (void*) (Md->PhysicalAddressStart + PAGE_SIZE);

    if (PagesForBitMap >= Md->NumberOfFreePages)
        return NULL;

    Md->NumberOfFreePages -= PagesForBitMap;
    Md->PhysicalAddressStart += PagesForBitMap * PAGE_SIZE;

    return BitMapAddr;
}

void PhysicalMemoryManager::InitializeMemoryDescriptors()
{
    uint64_t DescriptorCount = MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize;
    MemoryDescriptors        = (MemoryDescriptor*) AllocatePagesFromMemoryMap(
            (DescriptorCount * sizeof(MemoryDescriptor) + PAGE_SIZE - 1) / PAGE_SIZE);
    kmemset(MemoryDescriptors, 0, DescriptorCount * sizeof(MemoryDescriptor));

    for (uint64_t i = 0; i < DescriptorCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR* Desc
                = (EFI_MEMORY_DESCRIPTOR*) ((uint8_t*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        if (Desc->NumberOfPages <= 3) // Skip tiny regions
            continue;

        if (i == CurrentDescriptor)
        {
            MemoryDescriptors[i].PhysicalAddressStart
                    = Desc->PhysicalStart + (Desc->NumberOfPages - RemainingPagesInDescriptor) * PAGE_SIZE;
            MemoryDescriptors[i].TotalNumberOfPages = Desc->NumberOfPages;
            MemoryDescriptors[i].NumberOfFreePages  = RemainingPagesInDescriptor;
            MemoryDescriptors[i].BitMap = (uint8_t*) InitializeMemoryDescriptorBitMap(&MemoryDescriptors[i]);
            continue;
        }

        if (Desc->Type == EfiConventionalMemory)
        {
            MemoryDescriptors[i].PhysicalAddressStart = Desc->PhysicalStart;
            MemoryDescriptors[i].TotalNumberOfPages   = Desc->NumberOfPages;
            MemoryDescriptors[i].NumberOfFreePages    = Desc->NumberOfPages;
            MemoryDescriptors[i].BitMap = (uint8_t*) InitializeMemoryDescriptorBitMap(&MemoryDescriptors[i]);
        }
    }
}
