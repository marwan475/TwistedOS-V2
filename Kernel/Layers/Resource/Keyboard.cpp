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
constexpr uint16_t LINUX_SYN_REPORT = 0;

int32_t TranslateScanCodeToLinuxKeyCode(uint8_t ScanCodeBase)
{
    switch (ScanCodeBase)
    {
        case 0x01:
            return 1;
        case 0x02:
            return 2;
        case 0x03:
            return 3;
        case 0x04:
            return 4;
        case 0x05:
            return 5;
        case 0x06:
            return 6;
        case 0x07:
            return 7;
        case 0x08:
            return 8;
        case 0x09:
            return 9;
        case 0x0A:
            return 10;
        case 0x0B:
            return 11;
        case 0x0C:
            return 12;
        case 0x0D:
            return 13;
        case 0x0E:
            return 14;
        case 0x0F:
            return 15;
        case 0x10:
            return 16;
        case 0x11:
            return 17;
        case 0x12:
            return 18;
        case 0x13:
            return 19;
        case 0x14:
            return 20;
        case 0x15:
            return 21;
        case 0x16:
            return 22;
        case 0x17:
            return 23;
        case 0x18:
            return 24;
        case 0x19:
            return 25;
        case 0x1A:
            return 26;
        case 0x1B:
            return 27;
        case 0x1C:
            return 28;
        case 0x1D:
            return 29;
        case 0x1E:
            return 30;
        case 0x1F:
            return 31;
        case 0x20:
            return 32;
        case 0x21:
            return 33;
        case 0x22:
            return 34;
        case 0x23:
            return 35;
        case 0x24:
            return 36;
        case 0x25:
            return 37;
        case 0x26:
            return 38;
        case 0x27:
            return 39;
        case 0x28:
            return 40;
        case 0x29:
            return 41;
        case 0x2A:
            return 42;
        case 0x2B:
            return 43;
        case 0x2C:
            return 44;
        case 0x2D:
            return 45;
        case 0x2E:
            return 46;
        case 0x2F:
            return 47;
        case 0x30:
            return 48;
        case 0x31:
            return 49;
        case 0x32:
            return 50;
        case 0x33:
            return 51;
        case 0x34:
            return 52;
        case 0x35:
            return 53;
        case 0x36:
            return 54;
        case 0x37:
            return 55;
        case 0x38:
            return 56;
        case 0x39:
            return 57;
        case 0x3A:
            return 58;
        default:
            return -1;
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

Keyboard::Keyboard() : KeyboardTTY(nullptr), LeftShiftPressed(false), RightShiftPressed(false), CapsLockEnabled(false)
{
    HasPendingInterruptScanCode = false;
    PendingInterruptScanCode    = 0;
}

void Keyboard::Initialize(TTY* Terminal)
{
    KeyboardTTY       = Terminal;
    LeftShiftPressed  = false;
    RightShiftPressed = false;
    CapsLockEnabled   = false;
    HasPendingInterruptScanCode = false;
    PendingInterruptScanCode    = 0;

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

    DispatchEventInterrupt(ScanCode);

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

void Keyboard::DispatchEventInterrupt(uint8_t ScanCode)
{
    HasPendingInterruptScanCode = true;
    PendingInterruptScanCode    = ScanCode;

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
    uint8_t ScanCodeBase = static_cast<uint8_t>(ScanCode & 0x7F);
    int32_t LinuxKeyCode = TranslateScanCodeToLinuxKeyCode(ScanCodeBase);
    if (LinuxKeyCode < 0)
    {
        return false;
    }

    int32_t KeyValue = ((ScanCode & 0x80) != 0) ? 0 : 1;

    if (!EventManager->QueueInputEvent(Device, LINUX_EV_KEY, static_cast<uint16_t>(LinuxKeyCode), KeyValue))
    {
        return false;
    }

    return EventManager->QueueInputEvent(Device, LINUX_EV_SYN, LINUX_SYN_REPORT, 0);
}
