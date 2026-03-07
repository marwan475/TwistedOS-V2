#include <FileSystem.hpp>

FileSystem::FileSystem(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES* BootServices, Console* efiConsole)
    : ImageHandle(ImageHandle), BootServices(BootServices), efiConsole(efiConsole)
{
}

FileSystem::~FileSystem()
{
}

void FileSystem::OutputDirectoryInfo(EFI_FILE_PROTOCOL* Dir)
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

EFI_STATUS FileSystem::SetDirectoryPosition(EFI_FILE_PROTOCOL* Dir, EFI_FILE_PROTOCOL** NewDir,
                                            int index)
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

        OutputDirectoryInfo(*NewDir);
    }

    return status;
}

EFI_STATUS FileSystem::ReadFileSystem()
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

    OutputDirectoryInfo(Root);
    EFI_FILE_PROTOCOL* Dir = Root;
    EFI_FILE_PROTOCOL* NewDir;
    while (true)
    {
        char key = efiConsole->GetKeyOnEvent();

        int ikey = (key - '0') + 1; // offset for somereason

        if (ikey == 10)
            break;

        SetDirectoryPosition(Dir, &NewDir, ikey);
        Dir = NewDir;
    }

    return status;
}