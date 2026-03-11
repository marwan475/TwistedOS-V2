#pragma once

#include <Console.hpp>
#include <MemoryManager.hpp>
#include <uefi.hpp>

typedef struct
{
    MemoryMapInfo                     MemoryMap;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GopMode;
    UINTN                             StackVirtualAddrEnd;
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
    EFI_STATUS SetupForKernel(EFI_FILE_PROTOCOL* Dir, EFI_FILE_INFO FileInfo);
    EFI_STATUS SetDirectoryPosition(EFI_FILE_PROTOCOL* Dir, EFI_FILE_PROTOCOL** NewDir, int index);
    void       OutputDirectoryInfo(EFI_FILE_PROTOCOL* Dir);
    ~FileSystem();
};