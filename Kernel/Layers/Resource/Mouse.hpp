/**
 * File: Mouse.hpp
 * Description: PS/2 mouse input handling and translation into Linux-style evdev events.
 */

#pragma once

#include <stdint.h>

struct EventDevice;

class Mouse
{
private:
    bool    HasPendingPacket;
    uint8_t PendingPacket[3];
    uint8_t PacketIndex;

    bool InitializeController();
    bool SendMouseCommand(uint8_t Command);
    bool ReadControllerDataByte(uint8_t* Value, uint32_t SpinCountLimit);
    bool WaitForControllerInputBufferReady(uint32_t SpinCountLimit);
    void DispatchEventInterrupt();

public:
    Mouse();

    void Initialize();
    void HandleInterrupt();

    static bool HandleEventInterrupt(EventDevice* Device, void* OriginalDevice);
};
