/**
 * File: Keyboard.hpp
 * Author: Marwan Mostafa
 * Description: PS/2 keyboard input handling and translation into TTY buffer characters.
 */

#pragma once

#include <stdint.h>

class TTY;
struct EventDevice;

class Keyboard
{
private:
    TTY* KeyboardTTY;
    bool LeftShiftPressed;
    bool RightShiftPressed;
    bool CapsLockEnabled;
    bool CtrlPressed;
    bool AltPressed;
    bool ExtendedScanCodeE0;
    bool HasPendingInterruptScanCode;
    uint8_t PendingInterruptScanCode;
    bool PendingIsExtended;

    void DispatchEventInterrupt(uint8_t ScanCode, bool IsExtended);

public:
    Keyboard();

    void Initialize(TTY* Terminal);
    void SetTTY(TTY* Terminal);
    void HandleInterrupt();

    static bool HandleEventInterrupt(EventDevice* Device, void* OriginalDevice);
};
