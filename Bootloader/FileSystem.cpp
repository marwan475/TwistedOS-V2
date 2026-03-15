#include <CommonUtils.hpp>
#include <FileSystem.hpp>

FileSystem::FileSystem(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES* BootServices, Console* efiConsole)
    : ImageHandle(ImageHandle), BootServices(BootServices), efiConsole(efiConsole)
{
}

FileSystem::~FileSystem()
{
}

void FileSystem::OutputDirectoryInfo(EFI_FILE_PROTOCOL* Dir)
{
    EFI_FILE_INFO FileInfo;
    UINTN         BuffSize = sizeof(FileInfo);
    Dir->Read(Dir, &BuffSize, &FileInfo);

    int NumEntries = 0;

    while (BuffSize > 0)
    {
        efiConsole->printf_("\r\nFileIndex: %d\r\n", NumEntries);
        efiConsole->printf_("Filename: ");
        CHAR16* name = FileInfo.FileName;
        while (*name)
        {
            efiConsole->printf_("%c", (char) *name);
            name++;
        }
        efiConsole->printf_("\r\n");
        efiConsole->printf_("Filesize: %d\r\n", FileInfo.FileSize);

        efiConsole->printf_("Filetype: ");
        (FileInfo.Attribute & EFI_FILE_DIRECTORY) ? efiConsole->printf_("Directory\r\n")
                                                  : efiConsole->printf_("File\r\n");

        BuffSize = sizeof(FileInfo);
        Dir->Read(Dir, &BuffSize, &FileInfo);
        NumEntries++;
    }
}

EFI_STATUS GetMemoryMapFromEfi(MemoryMapInfo* MemoryMap, EFI_BOOT_SERVICES* BootServices)
{
    EFI_STATUS status;

    status = BootServices->GetMemoryMap(&MemoryMap->MemoryMapSize, MemoryMap->MemoryMap, &MemoryMap->MapKey,
                                        &MemoryMap->DescriptorSize, &MemoryMap->DescriptorVersion);

    MemoryMap->MemoryMapSize += MemoryMap->DescriptorSize * 2;
    BootServices->AllocatePool(EfiLoaderData, MemoryMap->MemoryMapSize, (VOID**) &MemoryMap->MemoryMap);

    status = BootServices->GetMemoryMap(&MemoryMap->MemoryMapSize, MemoryMap->MemoryMap, &MemoryMap->MapKey,
                                        &MemoryMap->DescriptorSize, &MemoryMap->DescriptorVersion);

    return status;
}

void PrintMemoryMap(MemoryMapInfo MemoryMap, Console* efiConsole)
{
    efiConsole->printf_("Memory map: Size %u, Descriptor size: %u, # of descriptors: %u, key: %x\r\n",
                        MemoryMap.MemoryMapSize, MemoryMap.DescriptorSize,
                        MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize, MemoryMap.MapKey);

    UINTN usable_bytes = 0; // "Usable" memory for an OS or similar, not firmware/device reserved
    for (UINTN i = 0; i < MemoryMap.MemoryMapSize / MemoryMap.DescriptorSize; i++)
    {
        EFI_MEMORY_DESCRIPTOR* desc
                = (EFI_MEMORY_DESCRIPTOR*) ((UINT8*) MemoryMap.MemoryMap + (i * MemoryMap.DescriptorSize));

        efiConsole->printf_("%u: Typ: %u, Phy: %x, Vrt: %x, Pgs: %u, Att: %x\r\n", i, desc->Type, desc->PhysicalStart,
                            desc->VirtualStart, desc->NumberOfPages, desc->Attribute);

        // Add to usable memory count depending on type
        if (desc->Type == EfiLoaderCode || desc->Type == EfiLoaderData || desc->Type == EfiBootServicesCode
            || desc->Type == EfiBootServicesData || desc->Type == EfiConventionalMemory
            || desc->Type == EfiPersistentMemory)
        {
            usable_bytes += desc->NumberOfPages * 4096;
        }
    }

    efiConsole->printf_("\r\nUsable memory: %u / %u MiB / %u GiB\r\n", usable_bytes, usable_bytes / (1024 * 1024),
                        usable_bytes / (1024 * 1024 * 1024));
}

EFI_STATUS FileSystem::LoadFileToMemory(EFI_FILE_PROTOCOL* Dir, CHAR16* Path, EFI_MEMORY_TYPE MemoryType, void** Buffer,
                                        UINTN* BufferSize)
{
    EFI_STATUS         status;
    EFI_FILE_PROTOCOL* File;
    status = Dir->Open(Dir, &File, Path, EFI_FILE_MODE_READ, 0);

    if (EFI_ERROR(status))
    {
        efiConsole->printf_("Failed to open file\r\n");
        return status;
    }

    EFI_GUID      FileInfoGuid = EFI_FILE_INFO_ID;
    EFI_FILE_INFO FileInfo;
    UINTN         FileInfoSize = sizeof(FileInfo);
    status                     = File->GetInfo(File, &FileInfoGuid, &FileInfoSize, &FileInfo);

    if (EFI_ERROR(status))
    {
        efiConsole->printf_("Failed to get file info\r\n");
        File->Close(File);
        return status;
    }

    UINTN                FileSize       = FileInfo.FileSize;
    UINTN                FilePages      = (FileSize + PAGE_SIZE - 1) / PAGE_SIZE;
    UINTN                FileAllocSize  = FilePages * PAGE_SIZE;
    EFI_PHYSICAL_ADDRESS FileBufferAddr = 0;
    void*                FileBuffer     = NULL;

    status     = BootServices->AllocatePages(AllocateAnyPages, MemoryType, FilePages, &FileBufferAddr);
    FileBuffer = (void*) FileBufferAddr;

    if (EFI_ERROR(status))
    {
        efiConsole->printf_("Failed to allocate file buffer\r\n");
        File->Close(File);
        return status;
    }

    kmemset(FileBuffer, 0, FileAllocSize);

    UINTN ReadSize = FileSize;

    status = File->Read(File, &ReadSize, FileBuffer);

    File->Close(File);

    if (EFI_ERROR(status))
    {
        efiConsole->printf_("Failed to read file\r\n");
        return status;
    }

    *Buffer     = FileBuffer;
    *BufferSize = FileSize;

    return EFI_SUCCESS;
}

EFI_STATUS FileSystem::SetupForKernel(EFI_FILE_PROTOCOL* Dir)
{
    EFI_STATUS status;

    void* KernelBuffer = NULL;
    UINTN KernelSize   = 0;

    status = LoadFileToMemory(Dir, (CHAR16*) u"kernel.bin", EfiLoaderCode, &KernelBuffer, &KernelSize);

    if (EFI_ERROR(status))
    {
        efiConsole->printf_("Failed to load kernel.bin\r\n");
        return status;
    }

    efiConsole->printf_("Kernel loaded at %p\r\n", KernelBuffer);

    void* InitramfsBuffer = NULL;
    UINTN InitramfsSize   = 0;

    status = LoadFileToMemory(Dir, (CHAR16*) u"initramfs.cpio", EfiLoaderData, &InitramfsBuffer, &InitramfsSize);

    if (EFI_ERROR(status))
    {
        efiConsole->printf_("Failed to load initramfs.cpio\r\n");
        return status;
    }

    efiConsole->printf_("Initramfs loaded at %p (%u bytes)\r\n", InitramfsBuffer, InitramfsSize);

    // Get memory map
    MemoryMapInfo MemoryMap = {0};
    GetMemoryMapFromEfi(&MemoryMap, BootServices);
    // PrintMemoryMap(MemoryMap, efiConsole);

    KernelParameters KernelArgs = {0};
    KernelArgs.GopMode          = efiConsole->GetGopMode();
    KernelArgs.MemoryMap        = MemoryMap;
    KernelArgs.InitramfsAddress = (UINTN) InitramfsBuffer;
    KernelArgs.InitramfsSize    = InitramfsSize;

    status = BootServices->ExitBootServices(ImageHandle, MemoryMap.MapKey);

    if (EFI_ERROR(status))
    {
        efiConsole->printf_("Failed to Exit Bootservices\r\n");
        return status;
    }

    MemoryManager MemoryMgr = MemoryManager(MemoryMap);

    MemoryMgr.IdentityMapMemoryMap();

    // Identity map the framebuffer
    MemoryMgr.IdentityMapRange(KernelArgs.GopMode.FrameBufferBase, KernelArgs.GopMode.FrameBufferSize);

    UINTN kernel_virtual_addr = KERNEL_BASE_VIRTUAL_ADDR;
    UINTN kernel_end_virtual  = MemoryMgr.MapKernelToHigherHalf((UINTN) KernelBuffer, KernelSize);

    KernelArgs.KernelEndVirtual = kernel_end_virtual;
    KernelArgs.PageMapL4Table   = MemoryMgr.GetPageMapL4Table();

    // Set up stack for the kernel
    void* stack_physical_addr = MemoryMgr.AllocateAvailablePagesFromMemoryMap(KERNEL_STACK_SIZE / PAGE_SIZE);
    UINTN stack_virtual_addr_end
            = MemoryMgr.IdentityMapRange((UINTN) stack_physical_addr, KERNEL_STACK_SIZE / PAGE_SIZE);

    KernelArgs.NextPageAddress            = MemoryMgr.GetNextPageAddress();
    KernelArgs.CurrentDescriptor          = MemoryMgr.GetCurrentDescriptor();
    KernelArgs.RemainingPagesInDescriptor = MemoryMgr.GetRemainingPagesInDescriptor();

    MemoryMgr.InitPaging();

    asm volatile("mov %0, %%rsp" ::"r"(stack_virtual_addr_end));

    void EFIAPI (*EntryPoint)(KernelParameters) = (void EFIAPI (*)(KernelParameters)) kernel_virtual_addr;

    EntryPoint(KernelArgs);

    return status;
}

EFI_STATUS FileSystem::LoadKernel()
{
    EFI_GUID                   LoadImageGUID = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL* LoadImageProtocol;

    EFI_STATUS status;

    status = BootServices->OpenProtocol(ImageHandle, &LoadImageGUID, (VOID**) &LoadImageProtocol, ImageHandle, NULL,
                                        EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

    if (EFI_ERROR(status))
    {
        efiConsole->printf_("Failed to Open LoadImageProtocol\r\n");
        return status;
    }

    EFI_GUID                         SimpleFileGUID = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* SimpleFileProtocol;

    status = BootServices->OpenProtocol(LoadImageProtocol->DeviceHandle, &SimpleFileGUID, (VOID**) &SimpleFileProtocol,
                                        ImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

    if (EFI_ERROR(status))
    {
        efiConsole->printf_("Failed to Open SimpleFileProtocol\r\n");
        return status;
    }

    // Open Root Dir
    EFI_FILE_PROTOCOL* Root = NULL;
    status                  = SimpleFileProtocol->OpenVolume(SimpleFileProtocol, &Root);

    if (EFI_ERROR(status))
    {
        efiConsole->printf_("Failed to Open Root Dir\r\n");
        return status;
    }

    OutputDirectoryInfo(Root);

    return SetupForKernel(Root);
}