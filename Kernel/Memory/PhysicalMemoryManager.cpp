#include <CommonUtils.hpp>
#include <Logging/FrameBufferConsole.hpp>
#include <Memory/PhysicalMemoryManager.hpp>

PhysicalMemoryManager::PhysicalMemoryManager(MemoryMapInfo MemoryMap, UINTN NextPageAddress, UINTN CurrentDescriptor, UINTN RemainingPagesInDescriptor)
    : MemoryMap(MemoryMap), NextPageAddress(NextPageAddress), CurrentDescriptor(CurrentDescriptor), RemainingPagesInDescriptor(RemainingPagesInDescriptor), TotalUsableMemory(0), TotalNumberOfPages(0),
      MemoryDescriptorInfo{0, 0, 0, NULL}
{
    InitializeMemoryStats();
}

void PhysicalMemoryManager::InitializeMemoryStats()
{
    UINTN DescriptorCount = MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize;

    for (UINTN i = 0; i < DescriptorCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR* Desc = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

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
            EFI_MEMORY_DESCRIPTOR* Desc = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

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

void* PhysicalMemoryManager::AllocatePagesFromDescriptor(UINTN Pages)
{
    if (Pages == 0)
        return NULL;

    if (MemoryDescriptorInfo.BitMap == NULL)
        return NULL;

    if (Pages > MemoryDescriptorInfo.NumberOfFreePages)
        return NULL;

    uint64_t RunStart = 0;
    uint64_t RunCount = 0;

    for (uint64_t PageIndex = 0; PageIndex < MemoryDescriptorInfo.TotalNumberOfPages; PageIndex++)
    {
        uint64_t ByteIndex = PageIndex / 8;
        uint64_t BitIndex  = PageIndex % 8;
        bool     IsUsed    = (MemoryDescriptorInfo.BitMap[ByteIndex] & (1u << BitIndex)) != 0;

        if (!IsUsed)
        {
            if (RunCount == 0)
                RunStart = PageIndex;

            RunCount++;

            if (RunCount == Pages)
            {
                for (uint64_t AllocPage = RunStart; AllocPage < RunStart + Pages; AllocPage++)
                {
                    uint64_t AllocByteIndex = AllocPage / 8;
                    uint64_t AllocBitIndex  = AllocPage % 8;
                    MemoryDescriptorInfo.BitMap[AllocByteIndex] |= (1u << AllocBitIndex);
                }

                MemoryDescriptorInfo.NumberOfFreePages -= Pages;
                return (void*) (MemoryDescriptorInfo.PhysicalAddressStart + (RunStart * PAGE_SIZE));
            }
        }
        else
        {
            RunCount = 0;
        }
    }

    return NULL;
}

bool PhysicalMemoryManager::FreePagesFromDescriptor(void* Address, UINTN Pages)
{
    if (Address == NULL || Pages == 0)
        return false;

    if (MemoryDescriptorInfo.BitMap == NULL)
        return false;

    uint64_t BaseAddress = MemoryDescriptorInfo.PhysicalAddressStart;
    uint64_t EndAddress  = BaseAddress + (MemoryDescriptorInfo.TotalNumberOfPages * PAGE_SIZE);
    uint64_t PhysAddr    = (uint64_t) Address;

    if (PhysAddr < BaseAddress || PhysAddr >= EndAddress)
        return false;

    if (((PhysAddr - BaseAddress) % PAGE_SIZE) != 0)
        return false;

    uint64_t StartPage = (PhysAddr - BaseAddress) / PAGE_SIZE;
    if (StartPage + Pages > MemoryDescriptorInfo.TotalNumberOfPages)
        return false;

    uint64_t FreedPages = 0;

    for (uint64_t PageIndex = StartPage; PageIndex < StartPage + Pages; PageIndex++)
    {
        uint64_t ByteIndex = PageIndex / 8;
        uint64_t BitIndex  = PageIndex % 8;
        uint8_t  Mask      = (uint8_t) (1u << BitIndex);

        if ((MemoryDescriptorInfo.BitMap[ByteIndex] & Mask) != 0)
        {
            MemoryDescriptorInfo.BitMap[ByteIndex] &= (uint8_t) ~Mask;
            FreedPages++;
        }
    }

    MemoryDescriptorInfo.NumberOfFreePages += FreedPages;
    if (MemoryDescriptorInfo.NumberOfFreePages > MemoryDescriptorInfo.TotalNumberOfPages)
    {
        MemoryDescriptorInfo.NumberOfFreePages = MemoryDescriptorInfo.TotalNumberOfPages;
    }

    return true;
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
        EFI_MEMORY_DESCRIPTOR* Desc = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        if (Desc->Type == EfiConventionalMemory)
        {
            ConventionalDescriptorCount++;
        }
    }

    Console.printf_("Conventional memory descriptors: %llu\n", (unsigned long long) ConventionalDescriptorCount);

    UINTN ConventionalDescriptorIndex = 0;

    for (UINTN i = 0; i < DescriptorCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR* Desc = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        if (Desc->Type != EfiConventionalMemory)
        {
            continue;
        }

        Console.printf_("Descriptor %llu: %llu pages\n", (unsigned long long) ConventionalDescriptorIndex, (unsigned long long) Desc->NumberOfPages);
        ConventionalDescriptorIndex++;
    }
}

void PhysicalMemoryManager::PrintMemoryDescriptors(FrameBufferConsole& Console) const
{
    if (MemoryDescriptorInfo.TotalNumberOfPages == 0)
    {
        Console.printf_("Memory descriptor not initialized\n");
        return;
    }

    Console.printf_("Largest descriptor: base=0x%llx total=%llu free=%llu bitmap=%s\n", (unsigned long long) MemoryDescriptorInfo.PhysicalAddressStart,
                    (unsigned long long) MemoryDescriptorInfo.TotalNumberOfPages, (unsigned long long) MemoryDescriptorInfo.NumberOfFreePages, MemoryDescriptorInfo.BitMap ? "yes" : "no");
}

void* InitializeMemoryDescriptorBitMap(MemoryDescriptor* Md)
{
    uint64_t BitMapSize = (Md->NumberOfFreePages + 7) / 8;

    // Allocate BitMap From the descriptor itself
    uint64_t PagesForBitMap = ((BitMapSize + PAGE_SIZE - 1) / PAGE_SIZE) + 1;
    void*    BitMapAddr     = (void*) (Md->PhysicalAddressStart + PAGE_SIZE);

    if (PagesForBitMap >= Md->NumberOfFreePages)
        return NULL;

    Md->NumberOfFreePages -= PagesForBitMap;
    Md->TotalNumberOfPages -= PagesForBitMap;
    Md->PhysicalAddressStart += PagesForBitMap * PAGE_SIZE;

    kmemset(BitMapAddr, 0, BitMapSize);

    return BitMapAddr;
}

void PhysicalMemoryManager::InitializeMemoryDescriptors()
{
    uint64_t DescriptorCount = MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize;

    MemoryDescriptorInfo.PhysicalAddressStart = 0;
    MemoryDescriptorInfo.TotalNumberOfPages   = 0;
    MemoryDescriptorInfo.NumberOfFreePages    = 0;
    MemoryDescriptorInfo.BitMap               = NULL;

    for (uint64_t i = 0; i < DescriptorCount; i++)
    {
        if (i <= CurrentDescriptor)
            continue; // skip descriptors that have been used by bootloader

        EFI_MEMORY_DESCRIPTOR* Desc = (EFI_MEMORY_DESCRIPTOR*) ((uint8_t*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        if (Desc->Type == EfiConventionalMemory && Desc->NumberOfPages > MemoryDescriptorInfo.TotalNumberOfPages)
        {
            MemoryDescriptorInfo.PhysicalAddressStart = Desc->PhysicalStart;
            MemoryDescriptorInfo.TotalNumberOfPages   = Desc->NumberOfPages;
            MemoryDescriptorInfo.NumberOfFreePages    = Desc->NumberOfPages;
            MemoryDescriptorInfo.BitMap               = NULL;
        }
    }

    MemoryDescriptorInfo.BitMap = (uint8_t*) InitializeMemoryDescriptorBitMap(&MemoryDescriptorInfo);
}
