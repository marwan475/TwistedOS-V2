#pragma once

#include <Console.hpp>
#include <uefi.hpp>

class FileSystem
{
private:
    EFI_HANDLE         ImageHandle;
    EFI_BOOT_SERVICES* BootServices;
    Console*           efiConsole;

public:
    FileSystem(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES* BootServices, Console* efiConsole);
    EFI_STATUS ReadFileSystem();
    EFI_STATUS SetDirectoryPosition(EFI_FILE_PROTOCOL* Dir, EFI_FILE_PROTOCOL** NewDir, int index);
    void OutputDirectoryInfo(EFI_FILE_PROTOCOL* Dir);
    ~FileSystem();
};