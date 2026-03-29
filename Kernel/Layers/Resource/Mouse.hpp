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
    uint32_t InitFailureCode;
    bool    ControllerInitialized;
    uint32_t DefaultsCommandResult;
    uint8_t DefaultsCommandResponse;
    uint32_t StreamingCommandResult;
    uint8_t StreamingCommandResponse;
    bool    HasPendingPacket;
    uint8_t PendingPacket[3];
    uint8_t PacketIndex;
    uint8_t PreviousButtonState;
    uint64_t InterruptCount;
    uint64_t PacketCount;
    uint64_t IgnoredIrqCount;
    uint64_t DecodeFailureCount;

    bool InitializeController();
    bool SendMouseCommand(uint8_t Command, uint32_t* OutResult, uint8_t* OutResponse);
    bool ReadControllerDataByte(uint8_t* Value, uint32_t SpinCountLimit);
    bool ReadMouseDataByte(uint8_t* Value, uint32_t SpinCountLimit);
    bool WaitForControllerInputBufferReady(uint32_t SpinCountLimit);
    void DrainControllerOutputBuffer(uint32_t MaxBytes);
    bool DispatchEventInterrupt();

public:
    Mouse();

    void Initialize();
    void HandleInterrupt();
    bool IsControllerInitialized() const;
    uint32_t GetInitFailureCode() const;
    uint32_t GetDefaultsCommandResult() const;
    uint8_t GetDefaultsCommandResponse() const;
    uint32_t GetStreamingCommandResult() const;
    uint8_t GetStreamingCommandResponse() const;

    static bool HandleEventInterrupt(EventDevice* Device, void* OriginalDevice);
};
