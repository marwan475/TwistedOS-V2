/**
 * File: FileSystem.hpp
 * Author: Marwan Mostafa
 * Description: Bootloader file system interface declarations.
 */

#pragma once

#include "../utils/KernelParameters.hpp"

#include <Console.hpp>
#include <MemoryManager.hpp>
#include <uefi.hpp>

class FileSystem
{
private:
    EFI_HANDLE         ImageHandle;
    EFI_BOOT_SERVICES* BootServices;
    Console*           efiConsole;

public:
    FileSystem(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES* BootServices, Console* efiConsole);
    EFI_STATUS LoadKernel();
    EFI_STATUS SetupForKernel(EFI_FILE_PROTOCOL* Dir);
    EFI_STATUS LoadFileToMemory(EFI_FILE_PROTOCOL* Dir, CHAR16* Path, EFI_MEMORY_TYPE MemoryType, void** Buffer, UINTN* BufferSize);
    void       OutputDirectoryInfo(EFI_FILE_PROTOCOL* Dir);
    ~FileSystem();
};