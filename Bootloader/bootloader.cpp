#include <Console.hpp>
#include <uefi.hpp>

Console* Con;

extern "C" void _putchar(char character)
{
    Con->putchar(character);
}

char* strcpy(char* dest, const char* src)
{
    char* d = dest;

    while ((*d++ = *src++))
        ;

    return dest;
}

void OutputDirectoryInfo(EFI_FILE_PROTOCOL* Dir, Console* efiConsole)
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

EFI_STATUS SetDirectoryPosition(EFI_FILE_PROTOCOL* Dir, EFI_FILE_PROTOCOL** NewDir, int index,
                                Console* efiConsole)
{
    EFI_STATUS status;

    Dir->SetPosition(Dir, 0);
    efiConsole->printf_("SETTING\r\n");
    int           NumEntries = 0;
    EFI_FILE_INFO FileInfo;
    UINTN         BuffSize;
    do
    {
        BuffSize = sizeof(FileInfo);
        Dir->Read(Dir, &BuffSize, &FileInfo);
        NumEntries++;
    } while (NumEntries < index);
    efiConsole->printf_("SETTING2\r\n");

    if (FileInfo.Attribute & EFI_FILE_DIRECTORY)
    {
        status = Dir->Open(Dir, NewDir, FileInfo.FileName, EFI_FILE_MODE_READ, 0);
        efiConsole->printf_("SETTING3\r\n");

        if (EFI_ERROR(status))
        {
            efiConsole->printf_("Failed to Open Dir\r\n");
            return status;
        }

        OutputDirectoryInfo(*NewDir, efiConsole);
    }

    return status;
}

EFI_STATUS ReadFiles(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES* BootServices, Console* efiConsole)
{
    EFI_GUID                   LoadImageGUID = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL* LoadImageProtocol;

    EFI_STATUS status;

    status = BootServices->OpenProtocol(ImageHandle, &LoadImageGUID, (VOID**) &LoadImageProtocol,
                                        ImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

    if (EFI_ERROR(status))
    {
        efiConsole->printf_("Failed to Open LoadImageProtocol\r\n");
        return status;
    }

    EFI_GUID                         SimpleFileGUID = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* SimpleFileProtocol;

    status = BootServices->OpenProtocol(LoadImageProtocol->DeviceHandle, &SimpleFileGUID,
                                        (VOID**) &SimpleFileProtocol, ImageHandle, NULL,
                                        EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);

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

    OutputDirectoryInfo(Root, efiConsole);
    EFI_FILE_PROTOCOL* Dir = Root;
    EFI_FILE_PROTOCOL* NewDir;
    while (true)
    {
        char key = efiConsole->GetKeyOnEvent();

        int ikey = (key - '0') + 1; // offset for somereason

        if (ikey == 10)
            break;

        SetDirectoryPosition(Dir, &NewDir, ikey, efiConsole);
        Dir = NewDir;
    }

    return status;
}

extern "C"
{
    EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
    {
        Console efiConsole
                = Console(SystemTable->ConOut, SystemTable->ConIn, SystemTable->BootServices);
        Con = &efiConsole;

        efiConsole.Reset();
        efiConsole.ClearConsole();
        efiConsole.printf_("Welcome to TwistedOS Bootloader\r\n");
        efiConsole.DisplayModeInfo();
        efiConsole.DisplayAllModeInfo();

        efiConsole.printf_("Pick a Text mode\r\n");
        char key = efiConsole.GetKeyOnEvent();

        int ikey = key - '0';

        // TODO: Error handling

        efiConsole.SetTextMode(ikey);

        efiConsole.ClearConsole();
        efiConsole.DisplayModeInfo();

        efiConsole.DisplayGraphicsModeInfo();
        efiConsole.DisplayAllGraphicsModeInfo();

        efiConsole.printf_("Pick a Graphics mode\r\n");

        efiConsole.printf_("Digit 1\r\n");
        key = efiConsole.GetKeyOnEvent();

        ikey = key - '0';

        int Gmode = ikey * 10;

        efiConsole.printf_("Digit 2\r\n");
        key = efiConsole.GetKeyOnEvent();

        ikey = key - '0';

        Gmode += ikey;

        efiConsole.SetGraphicsMode(Gmode);

        ReadFiles(ImageHandle, SystemTable->BootServices, &efiConsole);
        efiConsole.printf_("GOOD\r\n");

        while (true)
        {
        }

        return 0;
    }
}
