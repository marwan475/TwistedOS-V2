#pragma once

#include <Console.hpp>
#include <uefi.hpp>

typedef struct
{
    UINTN                  MemoryMapSize;
    EFI_MEMORY_DESCRIPTOR* MemoryMap;
    UINTN                  MapKey;
    UINTN                  DescriptorSize;
    UINT32                 DescriptorVersion;
} MemoryMapInfo;

typedef struct
{
    MemoryMapInfo                     MemoryMap;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GopMode;
} KernelParameters;

class FileSystem
{
private:
    EFI_HANDLE         ImageHandle;
    EFI_BOOT_SERVICES* BootServices;
    Console*           efiConsole;

public:
    FileSystem(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES* BootServices, Console* efiConsole);
    EFI_STATUS LoadKernel();
    EFI_STATUS SetDirectoryPosition(EFI_FILE_PROTOCOL* Dir, EFI_FILE_PROTOCOL** NewDir, int index);
    void       OutputDirectoryInfo(EFI_FILE_PROTOCOL* Dir);
    ~FileSystem();
};