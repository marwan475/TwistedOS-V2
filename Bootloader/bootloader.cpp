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
        Console efiConsole = Console(SystemTable->ConOut, SystemTable->ConIn);
        Con                = &efiConsole;

        efiConsole.Reset();
        efiConsole.ClearConsole();
        efiConsole.printf_("Welcome to TwistedOS Bootloader\r\n");
        efiConsole.DisplayModeInfo();
        efiConsole.DisplayAllModeInfo();

        EFI_EVENT events[1];

        events[0] = SystemTable->ConIn->WaitForKey;

        UINTN index      = 0;
        int   num_events = 1;

        while (true)
        {
            SystemTable->BootServices->WaitForEvent(num_events, events, &index);

            EFI_INPUT_KEY key;

            if (index == 0)
            {
                efiConsole.GetKeyFromUser(&key);
            }

            CHAR16 cbuf[2];
            cbuf[0] = key.UnicodeChar;
            cbuf[1] = '\0';

            efiConsole.printf_("Scancode: %x, Unicode: %s\r\n", key.ScanCode, cbuf);
        }

        while (true)
        {
        }

        return 0;
    }
}
