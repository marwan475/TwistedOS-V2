#include <Console.hpp>
#include <uefi.hpp>

Console* Con;

extern "C" void _putchar(char character)
{
    Con->putchar(character);
}

extern "C"
{
    EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
    {
        Console efiConsole = Console(SystemTable->ConOut, SystemTable->ConIn, SystemTable->BootServices);
        Con                = &efiConsole;

        efiConsole.Reset();
        efiConsole.ClearConsole();
        efiConsole.printf_("Welcome to TwistedOS Bootloader\r\n");
        efiConsole.DisplayModeInfo();
        efiConsole.DisplayAllModeInfo();

        efiConsole.printf_("Pick a display mode\r\n");
        char key = efiConsole.GetKeyOnEvent();

        int ikey = key - '0';

        //TODO: Error handling

        efiConsole.SetTextMode(ikey);
        
        efiConsole.ClearConsole();
        efiConsole.DisplayModeInfo();

        while (true)
        {
        }

        return 0;
    }
}
