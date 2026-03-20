/**
 * File: Keyboard.cpp
 * Author: Marwan Mostafa
 * Description: PS/2 keyboard input handling implementation.
 */

#include "Keyboard.hpp"

#include "TTY.hpp"

#include <Arch/x86.hpp>

namespace
{
constexpr uint16_t KEYBOARD_DATA_PORT                 = 0x60;
constexpr uint16_t KEYBOARD_STATUS_PORT               = 0x64;
constexpr uint8_t  KEYBOARD_STATUS_OUTPUT_BUFFER_FULL = 0x01;

constexpr uint8_t KEYBOARD_SCANCODE_LEFT_SHIFT_PRESS    = 0x2A;
constexpr uint8_t KEYBOARD_SCANCODE_RIGHT_SHIFT_PRESS   = 0x36;
constexpr uint8_t KEYBOARD_SCANCODE_LEFT_SHIFT_RELEASE  = 0xAA;
constexpr uint8_t KEYBOARD_SCANCODE_RIGHT_SHIFT_RELEASE = 0xB6;
constexpr uint8_t KEYBOARD_SCANCODE_CAPS_LOCK           = 0x3A;

bool IsAlphabeticalCharacter(char Character)
{
    return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z');
}

char ToggleCharacterCase(char Character)
{
    if (Character >= 'a' && Character <= 'z')
    {
        return static_cast<char>(Character - ('a' - 'A'));
    }

    if (Character >= 'A' && Character <= 'Z')
    {
        return static_cast<char>(Character + ('a' - 'A'));
    }

    return Character;
}

char KeyboardMapUnshifted[128] = {
        0, 27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',  '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,   '\\', 'z',  'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ',
};

char KeyboardMapShifted[128] = {
        0, 27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|',  'Z',  'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ',
};
} // namespace

Keyboard::Keyboard() : KeyboardTTY(nullptr), LeftShiftPressed(false), RightShiftPressed(false), CapsLockEnabled(false)
{
}

void Keyboard::Initialize(TTY* Terminal)
{
    KeyboardTTY       = Terminal;
    LeftShiftPressed  = false;
    RightShiftPressed = false;
    CapsLockEnabled   = false;

    while ((X86InB(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_OUTPUT_BUFFER_FULL) != 0)
    {
        (void) X86InB(KEYBOARD_DATA_PORT);
    }
}

void Keyboard::SetTTY(TTY* Terminal)
{
    KeyboardTTY = Terminal;
}

void Keyboard::HandleInterrupt()
{
    if ((X86InB(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_OUTPUT_BUFFER_FULL) == 0)
    {
        return;
    }

    uint8_t ScanCode = X86InB(KEYBOARD_DATA_PORT);

    if (ScanCode == KEYBOARD_SCANCODE_LEFT_SHIFT_PRESS)
    {
        LeftShiftPressed = true;
        return;
    }

    if (ScanCode == KEYBOARD_SCANCODE_RIGHT_SHIFT_PRESS)
    {
        RightShiftPressed = true;
        return;
    }

    if (ScanCode == KEYBOARD_SCANCODE_LEFT_SHIFT_RELEASE)
    {
        LeftShiftPressed = false;
        return;
    }

    if (ScanCode == KEYBOARD_SCANCODE_RIGHT_SHIFT_RELEASE)
    {
        RightShiftPressed = false;
        return;
    }

    if (ScanCode == KEYBOARD_SCANCODE_CAPS_LOCK)
    {
        CapsLockEnabled = !CapsLockEnabled;
        return;
    }

    if ((ScanCode & 0x80) != 0)
    {
        return;
    }

    if (ScanCode >= 128)
    {
        return;
    }

    bool ShiftPressed = LeftShiftPressed || RightShiftPressed;
    char Character    = ShiftPressed ? KeyboardMapShifted[ScanCode] : KeyboardMapUnshifted[ScanCode];

    if (CapsLockEnabled && IsAlphabeticalCharacter(Character))
    {
        Character = ToggleCharacterCase(Character);
    }

    if (Character == 0 || KeyboardTTY == nullptr)
    {
        return;
    }

    KeyboardTTY->PushKeyboardInputChar(Character);
}
