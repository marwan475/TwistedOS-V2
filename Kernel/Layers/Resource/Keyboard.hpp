/**
 * File: Keyboard.hpp
 * Author: Marwan Mostafa
 * Description: PS/2 keyboard input handling and translation into TTY buffer characters.
 */

#pragma once

#include <stdint.h>

class TTY;

class Keyboard
{
private:
    TTY* KeyboardTTY;
    bool LeftShiftPressed;
    bool RightShiftPressed;
    bool CapsLockEnabled;

public:
    Keyboard();

    void Initialize(TTY* Terminal);
    void SetTTY(TTY* Terminal);
    void HandleInterrupt();
};
