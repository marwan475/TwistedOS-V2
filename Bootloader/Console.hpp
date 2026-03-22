/**
 * File: Console.hpp
 * Author: Marwan Mostafa
 * Description: Bootloader console interface declarations.
 */

#pragma once

#include <uefi.hpp>

class Console
{
private:
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL*  ConsoleIn;
    EFI_BOOT_SERVICES*               BootServices;
    EFI_GRAPHICS_OUTPUT_PROTOCOL*    Gop;

public:
    Console(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut, EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn, EFI_BOOT_SERVICES* BootServices);
    void                              Reset();
    void                              ClearConsole();
    void                              ChangeColor(int forground, int background);
    void                              DisplayModeInfo();
    void                              DisplayAllModeInfo();
    void                              SetTextMode(int mode);
    int                               PickBestTextMode();
    void                              DisplayGraphicsModeInfo();
    void                              DisplayAllGraphicsModeInfo();
    void                              SetGraphicsMode(int mode);
    int                               PickGraphicsModeByResolution(UINT32 targetWidth, UINT32 targetHeight);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GetGopMode();
    EFI_STATUS                        GetKeyFromUser(EFI_INPUT_KEY* key);
    char                              GetKeyOnEvent();
    void                              putchar(char c);
    int                               printf_(const char* format, ...);
    ~Console();
};
