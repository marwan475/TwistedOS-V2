/**
 * File: Keyboard.cpp
 * Author: Marwan Mostafa
 * Description: PS/2 keyboard input handling implementation.
 */

#include "Keyboard.hpp"

#include "EventDeviceManager.hpp"
#include "TTY.hpp"

#include <Arch/x86.hpp>
#include <Layers/Dispatcher.hpp>
#include <Layers/Logic/LogicLayer.hpp>
#include <Layers/Resource/ResourceLayer.hpp>

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

constexpr uint16_t LINUX_EV_SYN = 0x00;
constexpr uint16_t LINUX_EV_KEY = 0x01;
constexpr uint16_t LINUX_EV_MSC = 0x04;
constexpr uint16_t LINUX_MSC_SCAN = 0x04;
constexpr uint16_t LINUX_SYN_REPORT = 0;

int32_t TranslateScanCodeToLinuxKeyCode(uint8_t ScanCodeBase)
{
    switch (ScanCodeBase)
    {
        case 0x01: return 1;   // KEY_ESC
        case 0x02: return 2;   // KEY_1
        case 0x03: return 3;   // KEY_2
        case 0x04: return 4;   // KEY_3
        case 0x05: return 5;   // KEY_4
        case 0x06: return 6;   // KEY_5
        case 0x07: return 7;   // KEY_6
        case 0x08: return 8;   // KEY_7
        case 0x09: return 9;   // KEY_8
        case 0x0A: return 10;  // KEY_9
        case 0x0B: return 11;  // KEY_0
        case 0x0C: return 12;  // KEY_MINUS
        case 0x0D: return 13;  // KEY_EQUAL
        case 0x0E: return 14;  // KEY_BACKSPACE
        case 0x0F: return 15;  // KEY_TAB
        case 0x10: return 16;  // KEY_Q
        case 0x11: return 17;  // KEY_W
        case 0x12: return 18;  // KEY_E
        case 0x13: return 19;  // KEY_R
        case 0x14: return 20;  // KEY_T
        case 0x15: return 21;  // KEY_Y
        case 0x16: return 22;  // KEY_U
        case 0x17: return 23;  // KEY_I
        case 0x18: return 24;  // KEY_O
        case 0x19: return 25;  // KEY_P
        case 0x1A: return 26;  // KEY_LEFTBRACE
        case 0x1B: return 27;  // KEY_RIGHTBRACE
        case 0x1C: return 28;  // KEY_ENTER
        case 0x1D: return 29;  // KEY_LEFTCTRL
        case 0x1E: return 30;  // KEY_A
        case 0x1F: return 31;  // KEY_S
        case 0x20: return 32;  // KEY_D
        case 0x21: return 33;  // KEY_F
        case 0x22: return 34;  // KEY_G
        case 0x23: return 35;  // KEY_H
        case 0x24: return 36;  // KEY_J
        case 0x25: return 37;  // KEY_K
        case 0x26: return 38;  // KEY_L
        case 0x27: return 39;  // KEY_SEMICOLON
        case 0x28: return 40;  // KEY_APOSTROPHE
        case 0x29: return 41;  // KEY_GRAVE
        case 0x2A: return 42;  // KEY_LEFTSHIFT
        case 0x2B: return 43;  // KEY_BACKSLASH
        case 0x2C: return 44;  // KEY_Z
        case 0x2D: return 45;  // KEY_X
        case 0x2E: return 46;  // KEY_C
        case 0x2F: return 47;  // KEY_V
        case 0x30: return 48;  // KEY_B
        case 0x31: return 49;  // KEY_N
        case 0x32: return 50;  // KEY_M
        case 0x33: return 51;  // KEY_COMMA
        case 0x34: return 52;  // KEY_DOT
        case 0x35: return 53;  // KEY_SLASH
        case 0x36: return 54;  // KEY_RIGHTSHIFT
        case 0x37: return 55;  // KEY_KPASTERISK
        case 0x38: return 56;  // KEY_LEFTALT
        case 0x39: return 57;  // KEY_SPACE
        case 0x3A: return 58;  // KEY_CAPSLOCK
        case 0x3B: return 59;  // KEY_F1
        case 0x3C: return 60;  // KEY_F2
        case 0x3D: return 61;  // KEY_F3
        case 0x3E: return 62;  // KEY_F4
        case 0x3F: return 63;  // KEY_F5
        case 0x40: return 64;  // KEY_F6
        case 0x41: return 65;  // KEY_F7
        case 0x42: return 66;  // KEY_F8
        case 0x43: return 67;  // KEY_F9
        case 0x44: return 68;  // KEY_F10
        case 0x45: return 69;  // KEY_NUMLOCK
        case 0x46: return 70;  // KEY_SCROLLLOCK
        case 0x47: return 71;  // KEY_KP7
        case 0x48: return 72;  // KEY_KP8
        case 0x49: return 73;  // KEY_KP9
        case 0x4A: return 74;  // KEY_KPMINUS
        case 0x4B: return 75;  // KEY_KP4
        case 0x4C: return 76;  // KEY_KP5
        case 0x4D: return 77;  // KEY_KP6
        case 0x4E: return 78;  // KEY_KPPLUS
        case 0x4F: return 79;  // KEY_KP1
        case 0x50: return 80;  // KEY_KP2
        case 0x51: return 81;  // KEY_KP3
        case 0x52: return 82;  // KEY_KP0
        case 0x53: return 83;  // KEY_KPDOT
        case 0x57: return 87;  // KEY_F11
        case 0x58: return 88;  // KEY_F12
        default:   return -1;
    }
}

int32_t TranslateExtendedScanCodeToLinuxKeyCode(uint8_t ScanCodeBase)
{
    switch (ScanCodeBase)
    {
        case 0x1C: return 96;  // KEY_KPENTER
        case 0x1D: return 97;  // KEY_RIGHTCTRL
        case 0x35: return 98;  // KEY_KPSLASH
        case 0x38: return 100; // KEY_RIGHTALT
        case 0x47: return 102; // KEY_HOME
        case 0x48: return 103; // KEY_UP
        case 0x49: return 104; // KEY_PAGEUP
        case 0x4B: return 105; // KEY_LEFT
        case 0x4D: return 106; // KEY_RIGHT
        case 0x4F: return 107; // KEY_END
        case 0x50: return 108; // KEY_DOWN
        case 0x51: return 109; // KEY_PAGEDOWN
        case 0x52: return 110; // KEY_INSERT
        case 0x53: return 111; // KEY_DELETE
        case 0x5B: return 125; // KEY_LEFTMETA
        case 0x5C: return 126; // KEY_RIGHTMETA
        case 0x5D: return 127; // KEY_COMPOSE
        default:   return -1;
    }
}

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

Keyboard::Keyboard() : KeyboardTTY(nullptr), LeftShiftPressed(false), RightShiftPressed(false), CapsLockEnabled(false), CtrlPressed(false), AltPressed(false), ExtendedScanCodeE0(false)
{
    HasPendingInterruptScanCode = false;
    PendingInterruptScanCode    = 0;
    PendingIsExtended           = false;
}

void Keyboard::Initialize(TTY* Terminal)
{
    KeyboardTTY       = Terminal;
    LeftShiftPressed  = false;
    RightShiftPressed = false;
    CapsLockEnabled   = false;
    CtrlPressed       = false;
    AltPressed        = false;
    ExtendedScanCodeE0 = false;
    HasPendingInterruptScanCode = false;
    PendingInterruptScanCode    = 0;
    PendingIsExtended           = false;

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

    if (ScanCode == 0xE0)
    {
        ExtendedScanCodeE0 = true;
        return;
    }

    bool IsExtended = ExtendedScanCodeE0;
    ExtendedScanCodeE0 = false;

    DispatchEventInterrupt(ScanCode, IsExtended);

    bool IsGrabbed = false;
    EventDevice* GrabDevice = nullptr;
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr)
        {
            EventDeviceManager* EventManager = ActiveDispatcher->GetResourceLayer()->GetEventDeviceManager();
            if (EventManager != nullptr)
            {
                GrabDevice = EventManager->GetEventDeviceByOriginalDevice(this);
                if (GrabDevice != nullptr && GrabDevice->Grabbed)
                {
                    IsGrabbed = true;
                }
            }
        }
    }

    bool IsRelease = (ScanCode & 0x80) != 0;
    uint8_t ScanCodeBase = static_cast<uint8_t>(ScanCode & 0x7F);

    if (KeyboardTTY != nullptr)
    {
        KeyboardTTY->Serialprintf("kb_dbg: sc=0x%02x base=0x%02x ext=%d rel=%d grabbed=%d dev=%d shift=%d alt=%d ctrl=%d\n",
            ScanCode, ScanCodeBase, IsExtended ? 1 : 0, IsRelease ? 1 : 0, IsGrabbed ? 1 : 0,
            GrabDevice != nullptr ? 1 : 0,
            (LeftShiftPressed || RightShiftPressed) ? 1 : 0, AltPressed ? 1 : 0, CtrlPressed ? 1 : 0);
    }

    if (IsExtended)
    {
        if (ScanCodeBase == 0x1D)
        {
            CtrlPressed = !IsRelease;
            return;
        }

        if (ScanCodeBase == 0x38)
        {
            AltPressed = !IsRelease;
            return;
        }

        if (IsRelease || KeyboardTTY == nullptr || IsGrabbed)
        {
            return;
        }

        const char* EscapeSequence = nullptr;
        switch (ScanCodeBase)
        {
            case 0x48: EscapeSequence = "\033[A";  break; // Up
            case 0x50: EscapeSequence = "\033[B";  break; // Down
            case 0x4D: EscapeSequence = "\033[C";  break; // Right
            case 0x4B: EscapeSequence = "\033[D";  break; // Left
            case 0x47: EscapeSequence = "\033[H";  break; // Home
            case 0x4F: EscapeSequence = "\033[F";  break; // End
            case 0x52: EscapeSequence = "\033[2~"; break; // Insert
            case 0x53: EscapeSequence = "\033[3~"; break; // Delete
            case 0x49: EscapeSequence = "\033[5~"; break; // Page Up
            case 0x51: EscapeSequence = "\033[6~"; break; // Page Down
            default: break;
        }

        if (EscapeSequence != nullptr)
        {
            uint64_t Length = 0;
            while (EscapeSequence[Length] != '\0')
            {
                ++Length;
            }

            KeyboardTTY->PushKeyboardInput(EscapeSequence, Length);
        }

        return;
    }

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

    if (ScanCodeBase == 0x1D)
    {
        CtrlPressed = !IsRelease;
        return;
    }

    if (ScanCodeBase == 0x38)
    {
        AltPressed = !IsRelease;
        return;
    }

    if (IsRelease)
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

    if (Character == 0 || KeyboardTTY == nullptr || IsGrabbed)
    {
        return;
    }

    if (CtrlPressed)
    {
        if (Character >= 'a' && Character <= 'z')
        {
            Character = static_cast<char>(Character - 'a' + 1);
        }
        else if (Character >= 'A' && Character <= 'Z')
        {
            Character = static_cast<char>(Character - 'A' + 1);
        }
    }

    KeyboardTTY->Serialprintf("kb_dbg: tty_char='%c' (0x%02x)\n", Character, static_cast<uint8_t>(Character));
    KeyboardTTY->PushKeyboardInputChar(Character);
}

void Keyboard::DispatchEventInterrupt(uint8_t ScanCode, bool IsExtended)
{
    HasPendingInterruptScanCode = true;
    PendingInterruptScanCode    = ScanCode;
    PendingIsExtended           = IsExtended;

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        HasPendingInterruptScanCode = false;
        return;
    }

    ResourceLayer* ActiveResourceLayer = ActiveDispatcher->GetResourceLayer();
    if (ActiveResourceLayer == nullptr)
    {
        HasPendingInterruptScanCode = false;
        return;
    }

    EventDeviceManager* EventManager = ActiveResourceLayer->GetEventDeviceManager();
    if (EventManager == nullptr)
    {
        HasPendingInterruptScanCode = false;
        return;
    }

    EventDevice* Device = EventManager->GetEventDeviceByOriginalDevice(this);
    if (Device == nullptr || Device->HandleIntrrupt == nullptr)
    {
        HasPendingInterruptScanCode = false;
        return;
    }

    Device->HandleIntrrupt(Device, this);
    HasPendingInterruptScanCode = false;
}

bool Keyboard::HandleEventInterrupt(EventDevice* Device, void* OriginalDevice)
{
    if (Device == nullptr || OriginalDevice == nullptr)
    {
        return false;
    }

    Keyboard* KeyboardDevice = reinterpret_cast<Keyboard*>(OriginalDevice);
    if (!KeyboardDevice->HasPendingInterruptScanCode)
    {
        return false;
    }

    EventDeviceManager* EventManager = nullptr;
    Dispatcher*         ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher != nullptr && ActiveDispatcher->GetResourceLayer() != nullptr)
    {
        EventManager = ActiveDispatcher->GetResourceLayer()->GetEventDeviceManager();
    }

    if (EventManager == nullptr)
    {
        return false;
    }

    uint8_t ScanCode = KeyboardDevice->PendingInterruptScanCode;
    bool    IsExtended = KeyboardDevice->PendingIsExtended;
    uint8_t ScanCodeBase = static_cast<uint8_t>(ScanCode & 0x7F);
    int32_t LinuxKeyCode = IsExtended ? TranslateExtendedScanCodeToLinuxKeyCode(ScanCodeBase) : TranslateScanCodeToLinuxKeyCode(ScanCodeBase);
    if (LinuxKeyCode < 0)
    {
        return false;
    }

    int32_t KeyValue = ((ScanCode & 0x80) != 0) ? 0 : 1;

    if (!EventManager->QueueInputEvent(Device, LINUX_EV_MSC, LINUX_MSC_SCAN, static_cast<int32_t>(ScanCodeBase)))
    {
        return false;
    }

    if (!EventManager->QueueInputEvent(Device, LINUX_EV_KEY, static_cast<uint16_t>(LinuxKeyCode), KeyValue))
    {
        return false;
    }

    return EventManager->QueueInputEvent(Device, LINUX_EV_SYN, LINUX_SYN_REPORT, 0);
}
