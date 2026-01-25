#pragma once

#include <uefi.hpp>

class Console
{
private:
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConsoleOut;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL*  ConsoleIn;

public:
    Console(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut, EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn);
    void       Reset();
    void       ClearConsole();
    void       ChangeColor(int forground, int background);
    void       DisplayModeInfo();
    void       DisplayAllModeInfo();
    EFI_STATUS GetKeyFromUser(EFI_INPUT_KEY* key);
    void       putchar(char c);
    int        printf_(const char* format, ...);
    ~Console();
};
