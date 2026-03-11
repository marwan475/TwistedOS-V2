#include <CommonUtils.hpp>
#include <Console.hpp>
#include <FileSystem.hpp>
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

        efiConsole.printf_("Pick a Text mode\r\n");
        // char key  = efiConsole.GetKeyOnEvent();
        char key  = '4';
        int  ikey = key - '0';

        efiConsole.SetTextMode(ikey);

        efiConsole.ClearConsole();
        efiConsole.DisplayModeInfo();

        efiConsole.DisplayGraphicsModeInfo();
        efiConsole.DisplayAllGraphicsModeInfo();

        efiConsole.printf_("Pick a Graphics mode\r\n");

        efiConsole.printf_("Digit 1\r\n");
        // key       = efiConsole.GetKeyOnEvent();
        key       = '2';
        ikey      = key - '0';
        int Gmode = ikey * 10;

        efiConsole.printf_("Digit 2\r\n");
        // key  = efiConsole.GetKeyOnEvent();
        key  = '2';
        ikey = key - '0';
        Gmode += ikey;

        efiConsole.SetGraphicsMode(Gmode);
        FileSystem efiFileSystem = FileSystem(ImageHandle, SystemTable->BootServices, &efiConsole);
        efiFileSystem.LoadKernel();

        efiConsole.printf_("End\r\n");

        while (true)
        {
        }

        return 0;
    }
}
