#include <Console.hpp>
#include <printf.hpp>

Console::Console(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut, EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn, EFI_BOOT_SERVICES* BootServices)
    : ConsoleOut(ConOut), ConsoleIn(ConIn), BootServices(BootServices)
{
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
