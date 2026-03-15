#include <Console.hpp>
#include <printf.hpp>

Console::Console(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut, EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn, EFI_BOOT_SERVICES* BootServices) : ConsoleOut(ConOut), ConsoleIn(ConIn), BootServices(BootServices)
{
    Gop               = NULL;
    EFI_GUID Gop_GUID = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

    EFI_STATUS status = 0;
    status            = BootServices->LocateProtocol(&Gop_GUID, NULL, (VOID**) &Gop);
    if (EFI_ERROR(status))
    {
        printf_("Could not locate GOP\r\n");
    }
}

Console::~Console()
{
}

void Console::Reset()
{
    ConsoleOut->Reset(ConsoleOut, false);
}

void Console::ClearConsole()
{
    ConsoleOut->ClearScreen(ConsoleOut);
}

void Console::ChangeColor(int forground, int background)
{
    ConsoleOut->SetAttribute(ConsoleOut, EFI_TEXT_ATTR(forground, background));
}

void Console::DisplayModeInfo()
{
    INT32 MaxMode = ConsoleOut->Mode->MaxMode;
    INT32 CurMode = ConsoleOut->Mode->Mode;

    UINTN Cols;
    UINTN Rows;

    ConsoleOut->QueryMode(ConsoleOut, (UINTN) CurMode, &Cols, &Rows);

    printf_("Max Mode: %d\r\n", MaxMode);
    printf_("Current Mode: %d\r\n", CurMode);
    printf_("Columns: %d\r\n", Cols);
    printf_("Rows: %d\r\n", Rows);
}

void Console::DisplayAllModeInfo()
{
    INT32 MaxMode = ConsoleOut->Mode->MaxMode;

    UINTN Cols;
    UINTN Rows;

    INT32 i;

    for (i = 0; i < MaxMode; i++)
    {
        ConsoleOut->QueryMode(ConsoleOut, (UINTN) i, &Cols, &Rows);
        printf_("\r\n");
        printf_("Mode: %d\r\n", i);
        printf_("Columns: %d\r\n", Cols);
        printf_("Rows: %d\r\n", Rows);
    }
}

void Console::SetTextMode(int mode)
{
    ConsoleOut->SetMode(ConsoleOut, mode);
}

void Console::DisplayGraphicsModeInfo()
{
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* GopModeInfo  = NULL;
    UINTN                                 ModeInfoSize = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

    Gop->QueryMode(Gop, Gop->Mode->Mode, &ModeInfoSize, &GopModeInfo);

    printf_("Max Mode %d\r\n", Gop->Mode->MaxMode);
    printf_("Current Mode %d\r\n", Gop->Mode->Mode);
    printf_("Width x Height %ux%u\r\n", GopModeInfo->HorizontalResolution, GopModeInfo->VerticalResolution);
    printf_("FrameBuffer Address 0x%x\r\n", Gop->Mode->FrameBufferBase);
    printf_("FrameBuffer Size %u\r\n", Gop->Mode->FrameBufferSize);
    printf_("Pixel Format %d\r\n", GopModeInfo->PixelFormat);
    printf_("Pixels Per Scan line %u\r\n", GopModeInfo->PixelsPerScanLine);
}

void Console::DisplayAllGraphicsModeInfo()
{
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* GopModeInfo  = NULL;
    UINTN                                 ModeInfoSize = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

    INT32 MaxMode = Gop->Mode->MaxMode;
    INT32 i;
    for (i = 0; i < MaxMode; i++)
    {
        Gop->QueryMode(Gop, (UINTN) i, &ModeInfoSize, &GopModeInfo);
        printf_("Mode %d: Width x Height %ux%u\r\n", i, GopModeInfo->HorizontalResolution, GopModeInfo->VerticalResolution);
    }
}

void Console::SetGraphicsMode(int mode)
{
    Gop->SetMode(Gop, mode);

    ClearConsole();

    printf_("Graphics Mode Set \r\n");
    DisplayGraphicsModeInfo();
}

EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Console::GetGopMode()
{
    return *Gop->Mode;
}

EFI_STATUS Console::GetKeyFromUser(EFI_INPUT_KEY* key)
{
    EFI_STATUS ret = ConsoleIn->ReadKeyStroke(ConsoleIn, key);

    return ret;
}

char Console::GetKeyOnEvent()
{
    EFI_EVENT events[1];

    events[0] = ConsoleIn->WaitForKey;

    UINTN index      = 0;
    int   num_events = 1;

    BootServices->WaitForEvent(num_events, events, &index);

    EFI_INPUT_KEY key;

    if (index == 0)
    {
        GetKeyFromUser(&key);
    }

    return key.UnicodeChar;
}

void Console::putchar(char c)
{
    CHAR16 str[2];
    str[0] = (CHAR16) c;
    str[1] = 0;
    ConsoleOut->OutputString(ConsoleOut, str);
}

int Console::printf_(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    int ret = vprintf_proxy(format, args);

    va_end(args);
    return ret;
}
