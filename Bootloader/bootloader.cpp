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
        Console efiConsole = Console(SystemTable->ConOut);
        Con                = &efiConsole;

        efiConsole.Reset();
        efiConsole.ClearConsole();
        efiConsole.printf_("Welcome to TwistedOS Bootloader\r\n");
        efiConsole.DisplayModeInfo();

        while (true)
        {
        }

        return 0;
    }
}
