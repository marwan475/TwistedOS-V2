/**
 * File: Mouse.cpp
 * Description: PS/2 mouse input handling implementation.
 */

#include "Mouse.hpp"

#include "EventDeviceManager.hpp"

#include <Arch/x86.hpp>
#include <Layers/Dispatcher.hpp>
#include <Layers/Logic/LogicLayer.hpp>
#include <Layers/Resource/ResourceLayer.hpp>

namespace
{
constexpr uint16_t PS2_DATA_PORT   = 0x60;
constexpr uint16_t PS2_STATUS_PORT = 0x64;
constexpr uint16_t PS2_CMD_PORT    = 0x64;

constexpr uint8_t PS2_STATUS_OUTPUT_BUFFER_FULL = 0x01;
constexpr uint8_t PS2_STATUS_INPUT_BUFFER_FULL  = 0x02;
constexpr uint8_t PS2_STATUS_MOUSE_DATA          = 0x20;

constexpr uint8_t PS2_CMD_ENABLE_AUX_DEVICE = 0xA8;
constexpr uint8_t PS2_CMD_READ_CONFIG_BYTE  = 0x20;
constexpr uint8_t PS2_CMD_WRITE_CONFIG_BYTE = 0x60;
constexpr uint8_t PS2_CMD_WRITE_MOUSE       = 0xD4;

constexpr uint8_t PS2_MOUSE_CMD_SET_DEFAULTS     = 0xF6;
constexpr uint8_t PS2_MOUSE_CMD_ENABLE_STREAMING = 0xF4;
constexpr uint8_t PS2_MOUSE_ACK                  = 0xFA;

constexpr uint8_t PS2_CONFIG_ENABLE_KEYBOARD_INTERRUPT = 1U << 0;
constexpr uint8_t PS2_CONFIG_ENABLE_MOUSE_INTERRUPT    = 1U << 1;
constexpr uint8_t PS2_CONFIG_DISABLE_MOUSE_CLOCK       = 1U << 5;

constexpr uint32_t PS2_DEFAULT_SPIN_COUNT = 200000;

constexpr uint16_t LINUX_EV_SYN      = 0x00;
constexpr uint16_t LINUX_EV_KEY      = 0x01;
constexpr uint16_t LINUX_EV_REL      = 0x02;
constexpr uint16_t LINUX_SYN_REPORT  = 0;
constexpr uint16_t LINUX_REL_X       = 0x00;
constexpr uint16_t LINUX_REL_Y       = 0x01;
constexpr uint16_t LINUX_BTN_LEFT    = 0x110;
constexpr uint16_t LINUX_BTN_RIGHT   = 0x111;
constexpr uint16_t LINUX_BTN_MIDDLE  = 0x112;

constexpr uint8_t PS2_PACKET_LEFT_BUTTON   = 0x01;
constexpr uint8_t PS2_PACKET_RIGHT_BUTTON  = 0x02;
constexpr uint8_t PS2_PACKET_MIDDLE_BUTTON = 0x04;
constexpr uint8_t PS2_PACKET_ALWAYS_ONE    = 0x08;
constexpr uint8_t PS2_PACKET_X_OVERFLOW    = 0x40;
constexpr uint8_t PS2_PACKET_Y_OVERFLOW    = 0x80;
} // namespace

Mouse::Mouse() : HasPendingPacket(false), PendingPacket{}, PacketIndex(0)
{
}

void Mouse::Initialize()
{
    HasPendingPacket = false;
    PacketIndex      = 0;

    (void) InitializeController();
}

bool Mouse::ReadControllerDataByte(uint8_t* Value, uint32_t SpinCountLimit)
{
    if (Value == nullptr)
    {
        return false;
    }

    for (uint32_t SpinCount = 0; SpinCount < SpinCountLimit; ++SpinCount)
    {
        if ((X86InB(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER_FULL) != 0)
        {
            *Value = X86InB(PS2_DATA_PORT);
            return true;
        }
    }

    return false;
}

bool Mouse::WaitForControllerInputBufferReady(uint32_t SpinCountLimit)
{
    for (uint32_t SpinCount = 0; SpinCount < SpinCountLimit; ++SpinCount)
    {
        if ((X86InB(PS2_STATUS_PORT) & PS2_STATUS_INPUT_BUFFER_FULL) == 0)
        {
            return true;
        }
    }

    return false;
}

bool Mouse::SendMouseCommand(uint8_t Command)
{
    if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
    {
        return false;
    }

    X86OutB(PS2_CMD_PORT, PS2_CMD_WRITE_MOUSE);

    if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
    {
        return false;
    }

    X86OutB(PS2_DATA_PORT, Command);

    uint8_t Response = 0;
    if (!ReadControllerDataByte(&Response, PS2_DEFAULT_SPIN_COUNT))
    {
        return false;
    }

    return Response == PS2_MOUSE_ACK;
}

bool Mouse::InitializeController()
{
    if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
    {
        return false;
    }

    X86OutB(PS2_CMD_PORT, PS2_CMD_ENABLE_AUX_DEVICE);

    if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
    {
        return false;
    }

    X86OutB(PS2_CMD_PORT, PS2_CMD_READ_CONFIG_BYTE);

    uint8_t ControllerConfig = 0;
    if (!ReadControllerDataByte(&ControllerConfig, PS2_DEFAULT_SPIN_COUNT))
    {
        return false;
    }

    ControllerConfig |= static_cast<uint8_t>(PS2_CONFIG_ENABLE_KEYBOARD_INTERRUPT | PS2_CONFIG_ENABLE_MOUSE_INTERRUPT);
    ControllerConfig &= static_cast<uint8_t>(~PS2_CONFIG_DISABLE_MOUSE_CLOCK);

    if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
    {
        return false;
    }

    X86OutB(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG_BYTE);

    if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
    {
        return false;
    }

    X86OutB(PS2_DATA_PORT, ControllerConfig);

    return SendMouseCommand(PS2_MOUSE_CMD_SET_DEFAULTS) && SendMouseCommand(PS2_MOUSE_CMD_ENABLE_STREAMING);
}

void Mouse::HandleInterrupt()
{
    if ((X86InB(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER_FULL) == 0)
    {
        return;
    }

    uint8_t Status = X86InB(PS2_STATUS_PORT);
    if ((Status & PS2_STATUS_MOUSE_DATA) == 0)
    {
        return;
    }

    uint8_t DataByte = X86InB(PS2_DATA_PORT);

    if (PacketIndex == 0 && (DataByte & PS2_PACKET_ALWAYS_ONE) == 0)
    {
        return;
    }

    PendingPacket[PacketIndex++] = DataByte;

    if (PacketIndex < 3)
    {
        return;
    }

    PacketIndex      = 0;
    HasPendingPacket = true;
    DispatchEventInterrupt();
    HasPendingPacket = false;
}

void Mouse::DispatchEventInterrupt()
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        return;
    }

    ResourceLayer* ActiveResourceLayer = ActiveDispatcher->GetResourceLayer();
    if (ActiveResourceLayer == nullptr)
    {
        return;
    }

    EventDeviceManager* EventManager = ActiveResourceLayer->GetEventDeviceManager();
    if (EventManager == nullptr)
    {
        return;
    }

    EventDevice* Device = EventManager->GetEventDeviceByOriginalDevice(this);
    if (Device == nullptr || Device->HandleIntrrupt == nullptr)
    {
        return;
    }

    Device->HandleIntrrupt(Device, this);
}

bool Mouse::HandleEventInterrupt(EventDevice* Device, void* OriginalDevice)
{
    if (Device == nullptr || OriginalDevice == nullptr)
    {
        return false;
    }

    Mouse* MouseDevice = reinterpret_cast<Mouse*>(OriginalDevice);
    if (!MouseDevice->HasPendingPacket)
    {
        return false;
    }

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        return false;
    }

    ResourceLayer* ActiveResourceLayer = ActiveDispatcher->GetResourceLayer();
    if (ActiveResourceLayer == nullptr)
    {
        return false;
    }

    EventDeviceManager* EventManager = ActiveResourceLayer->GetEventDeviceManager();
    if (EventManager == nullptr)
    {
        return false;
    }

    uint8_t Packet0 = MouseDevice->PendingPacket[0];
    uint8_t Packet1 = MouseDevice->PendingPacket[1];
    uint8_t Packet2 = MouseDevice->PendingPacket[2];

    if ((Packet0 & (PS2_PACKET_X_OVERFLOW | PS2_PACKET_Y_OVERFLOW)) != 0)
    {
        return EventManager->QueueInputEvent(Device, LINUX_EV_SYN, LINUX_SYN_REPORT, 0);
    }

    int32_t DeltaX = static_cast<int32_t>(static_cast<int8_t>(Packet1));
    int32_t DeltaY = -static_cast<int32_t>(static_cast<int8_t>(Packet2));

    bool EventQueued = true;

    if (DeltaX != 0)
    {
        EventQueued = EventManager->QueueInputEvent(Device, LINUX_EV_REL, LINUX_REL_X, DeltaX) && EventQueued;
    }

    if (DeltaY != 0)
    {
        EventQueued = EventManager->QueueInputEvent(Device, LINUX_EV_REL, LINUX_REL_Y, DeltaY) && EventQueued;
    }

    int32_t LeftButtonValue   = ((Packet0 & PS2_PACKET_LEFT_BUTTON) != 0) ? 1 : 0;
    int32_t RightButtonValue  = ((Packet0 & PS2_PACKET_RIGHT_BUTTON) != 0) ? 1 : 0;
    int32_t MiddleButtonValue = ((Packet0 & PS2_PACKET_MIDDLE_BUTTON) != 0) ? 1 : 0;

    EventQueued = EventManager->QueueInputEvent(Device, LINUX_EV_KEY, LINUX_BTN_LEFT, LeftButtonValue) && EventQueued;
    EventQueued = EventManager->QueueInputEvent(Device, LINUX_EV_KEY, LINUX_BTN_RIGHT, RightButtonValue) && EventQueued;
    EventQueued = EventManager->QueueInputEvent(Device, LINUX_EV_KEY, LINUX_BTN_MIDDLE, MiddleButtonValue) && EventQueued;

    return EventManager->QueueInputEvent(Device, LINUX_EV_SYN, LINUX_SYN_REPORT, 0) && EventQueued;
}
