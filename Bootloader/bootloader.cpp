/**
 * File: bootloader.cpp
 * Author: Marwan Mostafa
 * Description: Main UEFI bootloader entry point and setup flow.
 */

#include <CommonUtils.hpp>
#include <Console.hpp>
#include <FileSystem.hpp>
#include <uefi.hpp>

#ifndef BOOT_GFX_WIDTH
#define BOOT_GFX_WIDTH 1024
#endif

#ifndef BOOT_GFX_HEIGHT
#define BOOT_GFX_HEIGHT 768
#endif

Console* Con;

/**
 * Function: _putchar
 * Description: Required output hook for utils/printf that forwards a character to the active UEFI console.
 * Parameters:
 *   char character - Character to print to the console.
 * Returns:
 *   void - No value is returned.
 */
extern "C" void _putchar(char character)
{
    Con->putchar(character);
}

extern "C"
{
    /**
     * Function: efi_main
     * Description: UEFI boot entry point that initializes text/graphics (GOP) modes and calls FileSystem class to load the kernel.
     * Parameters:
     *   EFI_HANDLE ImageHandle - Handle to the currently loaded UEFI image.
     *   EFI_SYSTEM_TABLE* SystemTable - Pointer to the UEFI system table and boot services.
     * Returns:
     *   EFI_STATUS - UEFI status code; this implementation does not return during its current runtime flow.
     */
    EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
    {
        Console efiConsole = Console(SystemTable->ConOut, SystemTable->ConIn, SystemTable->BootServices);
        Con                = &efiConsole;

        efiConsole.Reset();
        efiConsole.ClearConsole();
        efiConsole.printf_("Welcome to TwistedOS Bootloader\r\n");
        efiConsole.DisplayModeInfo();
        efiConsole.DisplayAllModeInfo();

        int textMode = efiConsole.PickBestTextMode();
        efiConsole.printf_("Auto-selected text mode: %d\r\n", textMode);
        efiConsole.SetTextMode(textMode);

        efiConsole.ClearConsole();
        efiConsole.DisplayModeInfo();

        efiConsole.DisplayGraphicsModeInfo();
        efiConsole.DisplayAllGraphicsModeInfo();

        efiConsole.printf_("Target graphics resolution: %ux%u\r\n", (UINT32) BOOT_GFX_WIDTH, (UINT32) BOOT_GFX_HEIGHT);
        int graphicsMode = efiConsole.PickGraphicsModeByResolution((UINT32) BOOT_GFX_WIDTH, (UINT32) BOOT_GFX_HEIGHT);
        efiConsole.printf_("Auto-selected graphics mode: %d\r\n", graphicsMode);
        efiConsole.SetGraphicsMode(graphicsMode);
        FileSystem efiFileSystem = FileSystem(ImageHandle, SystemTable->BootServices, &efiConsole);
        efiFileSystem.LoadKernel();

        efiConsole.printf_("End\r\n");

        while (true)
        {
        }

        return 0;
    }
}
