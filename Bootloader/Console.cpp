/**
 * File: Console.cpp
 * Author: Marwan Mostafa
 * Description: Bootloader console output implementation.
 */

#include <Console.hpp>
#include <printf.hpp>

/**
 * Function: Console::Console
 * Description: Initializes console interfaces and locates the UEFI Graphics Output Protocol (GOP).
 * Parameters:
 *   EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut - UEFI text output protocol interface.
 *   EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn - UEFI text input protocol interface.
 *   EFI_BOOT_SERVICES* BootServices - UEFI boot services table used for protocol and event operations.
 * Returns:
 *   N/A - Constructor initializes the Console object.
 */
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

/**
 * Function: Console::~Console
 * Description: Destroys the Console object.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   N/A - Destructor does not return a value.
 */
Console::~Console()
{
}

/**
 * Function: Console::Reset
 * Description: Resets the UEFI text output console.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
void Console::Reset()
{
    ConsoleOut->Reset(ConsoleOut, false);
}

/**
 * Function: Console::ClearConsole
 * Description: Clears the UEFI text output screen.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
void Console::ClearConsole()
{
    ConsoleOut->ClearScreen(ConsoleOut);
}

/**
 * Function: Console::ChangeColor
 * Description: Sets the foreground and background colors for text output.
 * Parameters:
 *   int forground - Foreground color attribute value.
 *   int background - Background color attribute value.
 * Returns:
 *   void - No value is returned.
 */
void Console::ChangeColor(int forground, int background)
{
    ConsoleOut->SetAttribute(ConsoleOut, EFI_TEXT_ATTR(forground, background));
}

/**
 * Function: Console::DisplayModeInfo
 * Description: Displays information for the current UEFI text mode.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
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

/**
 * Function: Console::DisplayAllModeInfo
 * Description: Displays information for all available UEFI text modes.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
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

/**
 * Function: Console::SetTextMode
 * Description: Sets the active UEFI text mode by mode index.
 * Parameters:
 *   int mode - Text mode index to activate.
 * Returns:
 *   void - No value is returned.
 */
void Console::SetTextMode(int mode)
{
    ConsoleOut->SetMode(ConsoleOut, mode);
}

/**
 * Function: Console::PickBestTextMode
 * Description: Selects the text mode with the largest terminal area (columns * rows).
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   int - Best available text mode index.
 */
int Console::PickBestTextMode()
{
    INT32 MaxMode = ConsoleOut->Mode->MaxMode;

    UINTN bestCols = 0;
    UINTN bestRows = 0;
    UINTN bestArea = 0;
    INT32 bestMode = ConsoleOut->Mode->Mode;

    for (INT32 i = 0; i < MaxMode; i++)
    {
        UINTN      Cols   = 0;
        UINTN      Rows   = 0;
        EFI_STATUS status = ConsoleOut->QueryMode(ConsoleOut, (UINTN) i, &Cols, &Rows);
        if (EFI_ERROR(status))
        {
            continue;
        }

        UINTN area = Cols * Rows;
        if (area > bestArea || (area == bestArea && (Cols > bestCols || (Cols == bestCols && Rows > bestRows))))
        {
            bestArea = area;
            bestCols = Cols;
            bestRows = Rows;
            bestMode = i;
        }
    }

    return bestMode;
}

/**
 * Function: Console::DisplayGraphicsModeInfo
 * Description: Displays details about the current GOP graphics mode.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
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

/**
 * Function: Console::DisplayAllGraphicsModeInfo
 * Description: Displays resolution information for all available GOP graphics modes.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
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

/**
 * Function: Console::SetGraphicsMode
 * Description: Sets the GOP graphics mode and prints the updated mode information.
 * Parameters:
 *   int mode - Graphics mode index to activate.
 * Returns:
 *   void - No value is returned.
 */
void Console::SetGraphicsMode(int mode)
{
    Gop->SetMode(Gop, mode);

    ClearConsole();

    printf_("Graphics Mode Set \r\n");
    DisplayGraphicsModeInfo();
}

/**
 * Function: Console::PickGraphicsModeByResolution
 * Description: Selects the graphics mode that best matches a target resolution.
 * Parameters:
 *   UINT32 targetWidth - Preferred horizontal resolution.
 *   UINT32 targetHeight - Preferred vertical resolution.
 * Returns:
 *   int - Best matching graphics mode index.
 */
int Console::PickGraphicsModeByResolution(UINT32 targetWidth, UINT32 targetHeight)
{
    auto absDiff = [](UINT32 left, UINT32 right) -> UINT64 { return (left >= right) ? (UINT64) (left - right) : (UINT64) (right - left); };

    INT32 MaxMode  = Gop->Mode->MaxMode;
    INT32 bestMode = Gop->Mode->Mode;

    UINT64 bestDistance = ~0ULL;
    UINT64 bestArea     = 0;

    for (INT32 i = 0; i < MaxMode; i++)
    {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* GopModeInfo  = NULL;
        UINTN                                 ModeInfoSize = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

        EFI_STATUS status = Gop->QueryMode(Gop, (UINTN) i, &ModeInfoSize, &GopModeInfo);
        if (EFI_ERROR(status) || GopModeInfo == NULL)
        {
            continue;
        }

        UINT32 width  = GopModeInfo->HorizontalResolution;
        UINT32 height = GopModeInfo->VerticalResolution;

        if (width == targetWidth && height == targetHeight)
        {
            return i;
        }

        UINT64 distance = absDiff(width, targetWidth) + absDiff(height, targetHeight);
        UINT64 area     = (UINT64) width * (UINT64) height;

        if (distance < bestDistance || (distance == bestDistance && area > bestArea))
        {
            bestDistance = distance;
            bestArea     = area;
            bestMode     = i;
        }
    }

    return bestMode;
}

/**
 * Function: Console::GetGopMode
 * Description: Retrieves the current GOP mode structure.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE - Current GOP mode data.
 */
EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Console::GetGopMode()
{
    return *Gop->Mode;
}

/**
 * Function: Console::GetKeyFromUser
 * Description: Reads a key stroke from the UEFI text input device.
 * Parameters:
 *   EFI_INPUT_KEY* key - Output buffer for the key data read from input.
 * Returns:
 *   EFI_STATUS - UEFI status result from ReadKeyStroke.
 */
EFI_STATUS Console::GetKeyFromUser(EFI_INPUT_KEY* key)
{
    EFI_STATUS ret = ConsoleIn->ReadKeyStroke(ConsoleIn, key);

    return ret;
}

/**
 * Function: Console::GetKeyOnEvent
 * Description: Waits for a key event and returns the Unicode character from the received key.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   char - Unicode character value from the key event.
 */
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

/**
 * Function: Console::putchar
 * Description: Writes a single character to the UEFI text output console.
 * Parameters:
 *   char c - Character to output.
 * Returns:
 *   void - No value is returned.
 */
void Console::putchar(char c)
{
    CHAR16 str[2];
    str[0] = (CHAR16) c;
    str[1] = 0;
    ConsoleOut->OutputString(ConsoleOut, str);
}

/**
 * Function: Console::printf_
 * Description: Prints formatted text to the console using variadic arguments.
 * Parameters:
 *   const char* format - Format string used for output formatting.
 *   ... - Variadic arguments consumed by the formatter.
 * Returns:
 *   int - Number of characters written, as returned by vprintf_proxy.
 */
int Console::printf_(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    int ret = vprintf_proxy(format, args);

    va_end(args);
    return ret;
}
