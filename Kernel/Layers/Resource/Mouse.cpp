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
constexpr uint8_t PS2_MOUSE_RESEND               = 0xFE;

constexpr uint8_t PS2_CONFIG_ENABLE_KEYBOARD_INTERRUPT = 1U << 0;
constexpr uint8_t PS2_CONFIG_ENABLE_MOUSE_INTERRUPT    = 1U << 1;
constexpr uint8_t PS2_CONFIG_DISABLE_MOUSE_CLOCK       = 1U << 5;

constexpr uint32_t PS2_DEFAULT_SPIN_COUNT = 200000;
constexpr uint32_t PS2_MAX_AUX_WAIT_SPINS  = PS2_DEFAULT_SPIN_COUNT * 4;
constexpr uint32_t PS2_MAX_CMD_RETRIES     = 3;

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

constexpr uint64_t MOUSE_LOG_PACKET_INTERVAL = 64;
constexpr uint64_t MOUSE_LOG_IRQ_INTERVAL    = 256;

constexpr uint32_t MOUSE_INIT_OK                                  = 0;
constexpr uint32_t MOUSE_INIT_FAIL_WAIT_IBF_BEFORE_ENABLE_AUX     = 1;
constexpr uint32_t MOUSE_INIT_FAIL_WAIT_IBF_BEFORE_READ_CFG        = 2;
constexpr uint32_t MOUSE_INIT_FAIL_READ_CFG_TIMEOUT                = 3;
constexpr uint32_t MOUSE_INIT_FAIL_WAIT_IBF_BEFORE_WRITE_CFG_CMD   = 4;
constexpr uint32_t MOUSE_INIT_FAIL_WAIT_IBF_BEFORE_WRITE_CFG_DATA  = 5;
constexpr uint32_t MOUSE_INIT_FAIL_SET_DEFAULTS                    = 6;
constexpr uint32_t MOUSE_INIT_FAIL_ENABLE_STREAMING                = 7;

constexpr uint32_t MOUSE_CMD_RESULT_NONE                    = 0;
constexpr uint32_t MOUSE_CMD_RESULT_WAIT_IBF_BEFORE_D4      = 1;
constexpr uint32_t MOUSE_CMD_RESULT_WAIT_IBF_BEFORE_COMMAND = 2;
constexpr uint32_t MOUSE_CMD_RESULT_ACK                     = 3;
constexpr uint32_t MOUSE_CMD_RESULT_RESEND                  = 4;
constexpr uint32_t MOUSE_CMD_RESULT_UNEXPECTED_RESPONSE     = 5;
constexpr uint32_t MOUSE_CMD_RESULT_TIMEOUT_WAITING_ACK     = 6;
constexpr uint32_t MOUSE_CMD_RESULT_FAILED_AFTER_RETRIES    = 7;

TTY* ResolveMouseLogTTY()
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        return nullptr;
    }

    ResourceLayer* ActiveResourceLayer = ActiveDispatcher->GetResourceLayer();
    if (ActiveResourceLayer == nullptr)
    {
        return nullptr;
    }

    return ActiveResourceLayer->GetTTY();
}

FrameBufferConsole* ResolveMouseLogConsole()
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        return nullptr;
    }

    ResourceLayer* ActiveResourceLayer = ActiveDispatcher->GetResourceLayer();
    if (ActiveResourceLayer == nullptr)
    {
        return nullptr;
    }

    return ActiveResourceLayer->GetConsole();
}

template <typename... Args>
void MouseLogf(const char* Format, Args... Arguments)
{
    TTY* Terminal = ResolveMouseLogTTY();
    if (Terminal != nullptr)
    {
        Terminal->Serialprintf(Format, Arguments...);
        return;
    }

    FrameBufferConsole* Console = ResolveMouseLogConsole();
    if (Console != nullptr)
    {
        Console->printf_(Format, Arguments...);
    }
}
} // namespace

Mouse::Mouse()
        : InitFailureCode(MOUSE_INIT_OK), ControllerInitialized(false), DefaultsCommandResult(MOUSE_CMD_RESULT_NONE), DefaultsCommandResponse(0),
            StreamingCommandResult(MOUSE_CMD_RESULT_NONE), StreamingCommandResponse(0), HasPendingPacket(false), PendingPacket{}, PacketIndex(0), PreviousButtonState(0), InterruptCount(0), PacketCount(0), IgnoredIrqCount(0),
            DecodeFailureCount(0)
{
}

void Mouse::Initialize()
{
    InitFailureCode = MOUSE_INIT_OK;
    ControllerInitialized = false;
    DefaultsCommandResult = MOUSE_CMD_RESULT_NONE;
    DefaultsCommandResponse = 0;
    StreamingCommandResult = MOUSE_CMD_RESULT_NONE;
    StreamingCommandResponse = 0;
    HasPendingPacket    = false;
    PacketIndex          = 0;
    PreviousButtonState  = 0;
    InterruptCount       = 0;
    PacketCount      = 0;
    IgnoredIrqCount  = 0;
    DecodeFailureCount = 0;

    ControllerInitialized = InitializeController();
    MouseLogf("mouse_dbg: controller init %s\n", ControllerInitialized ? "ok" : "failed");
}

bool Mouse::IsControllerInitialized() const
{
    return ControllerInitialized;
}

uint32_t Mouse::GetInitFailureCode() const
{
    return InitFailureCode;
}

uint32_t Mouse::GetDefaultsCommandResult() const
{
    return DefaultsCommandResult;
}

uint8_t Mouse::GetDefaultsCommandResponse() const
{
    return DefaultsCommandResponse;
}

uint32_t Mouse::GetStreamingCommandResult() const
{
    return StreamingCommandResult;
}

uint8_t Mouse::GetStreamingCommandResponse() const
{
    return StreamingCommandResponse;
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

bool Mouse::ReadMouseDataByte(uint8_t* Value, uint32_t SpinCountLimit)
{
    if (Value == nullptr)
    {
        return false;
    }

    for (uint32_t SpinCount = 0; SpinCount < SpinCountLimit; ++SpinCount)
    {
        uint8_t Status = X86InB(PS2_STATUS_PORT);
        if ((Status & PS2_STATUS_OUTPUT_BUFFER_FULL) == 0)
        {
            continue;
        }

        uint8_t Data = X86InB(PS2_DATA_PORT);
        if ((Status & PS2_STATUS_MOUSE_DATA) != 0)
        {
            *Value = Data;
            return true;
        }

        // Drain unrelated keyboard/controller output while waiting for mouse ACK.
    }

    return false;
}

void Mouse::DrainControllerOutputBuffer(uint32_t MaxBytes)
{
    for (uint32_t Index = 0; Index < MaxBytes; ++Index)
    {
        uint8_t Status = X86InB(PS2_STATUS_PORT);
        if ((Status & PS2_STATUS_OUTPUT_BUFFER_FULL) == 0)
        {
            return;
        }

        (void) X86InB(PS2_DATA_PORT);
    }
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

bool Mouse::SendMouseCommand(uint8_t Command, uint32_t* OutResult, uint8_t* OutResponse)
{
    if (OutResult != nullptr)
    {
        *OutResult = MOUSE_CMD_RESULT_NONE;
    }
    if (OutResponse != nullptr)
    {
        *OutResponse = 0;
    }

    for (uint32_t Attempt = 0; Attempt < PS2_MAX_CMD_RETRIES; ++Attempt)
    {
        // Drop stale bytes so we do not mistake old controller output for ACK/RESEND.
        DrainControllerOutputBuffer(16);

        if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
        {
            if (OutResult != nullptr)
            {
                *OutResult = MOUSE_CMD_RESULT_WAIT_IBF_BEFORE_D4;
            }
            MouseLogf("mouse_dbg: cmd=0x%x attempt=%u fail=wait_ibf_before_d4\n", Command, Attempt + 1U);
            continue;
        }

        X86OutB(PS2_CMD_PORT, PS2_CMD_WRITE_MOUSE);

        if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
        {
            if (OutResult != nullptr)
            {
                *OutResult = MOUSE_CMD_RESULT_WAIT_IBF_BEFORE_COMMAND;
            }
            MouseLogf("mouse_dbg: cmd=0x%x attempt=%u fail=wait_ibf_before_cmd\n", Command, Attempt + 1U);
            continue;
        }

        X86OutB(PS2_DATA_PORT, Command);

        bool ShouldRetry = false;
        for (uint32_t Poll = 0; Poll < 16; ++Poll)
        {
            uint8_t Response = 0;
            bool GotResponse = ReadMouseDataByte(&Response, PS2_MAX_AUX_WAIT_SPINS / 16);
            if (!GotResponse)
            {
                // Some virtual controllers may deliver ACK without AUX status tagging.
                GotResponse = ReadControllerDataByte(&Response, PS2_DEFAULT_SPIN_COUNT / 16);
            }

            if (!GotResponse)
            {
                continue;
            }

            if (Response == PS2_MOUSE_ACK)
            {
                if (OutResult != nullptr)
                {
                    *OutResult = MOUSE_CMD_RESULT_ACK;
                }
                if (OutResponse != nullptr)
                {
                    *OutResponse = Response;
                }
                MouseLogf("mouse_dbg: cmd=0x%x attempt=%u ack poll=%u\n", Command, Attempt + 1U, Poll + 1U);
                return true;
            }

            if (Response == PS2_MOUSE_RESEND)
            {
                ShouldRetry = true;
                if (OutResult != nullptr)
                {
                    *OutResult = MOUSE_CMD_RESULT_RESEND;
                }
                if (OutResponse != nullptr)
                {
                    *OutResponse = Response;
                }
                MouseLogf("mouse_dbg: cmd=0x%x attempt=%u resend poll=%u\n", Command, Attempt + 1U, Poll + 1U);
                break;
            }

            if (OutResult != nullptr)
            {
                *OutResult = MOUSE_CMD_RESULT_UNEXPECTED_RESPONSE;
            }
            if (OutResponse != nullptr)
            {
                *OutResponse = Response;
            }
            MouseLogf("mouse_dbg: cmd=0x%x attempt=%u unexpected_resp=0x%x poll=%u\n", Command, Attempt + 1U, Response, Poll + 1U);
        }

        if (ShouldRetry)
        {
            continue;
        }

        if (OutResult != nullptr)
        {
            *OutResult = MOUSE_CMD_RESULT_TIMEOUT_WAITING_ACK;
        }
        MouseLogf("mouse_dbg: cmd=0x%x attempt=%u timeout_waiting_ack\n", Command, Attempt + 1U);
    }

    if (OutResult != nullptr)
    {
        *OutResult = MOUSE_CMD_RESULT_FAILED_AFTER_RETRIES;
    }
    MouseLogf("mouse_dbg: cmd=0x%x failed_after_retries=%u\n", Command, PS2_MAX_CMD_RETRIES);

    return false;
}

bool Mouse::InitializeController()
{
    bool UsedFallbackConfig = false;

    DrainControllerOutputBuffer(64);

    if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
    {
        InitFailureCode = MOUSE_INIT_FAIL_WAIT_IBF_BEFORE_ENABLE_AUX;
        MouseLogf("mouse_dbg: init_fail stage=wait_ibf_before_enable_aux\n");
        return false;
    }

    X86OutB(PS2_CMD_PORT, PS2_CMD_ENABLE_AUX_DEVICE);

    if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
    {
        InitFailureCode = MOUSE_INIT_FAIL_WAIT_IBF_BEFORE_READ_CFG;
        MouseLogf("mouse_dbg: init_fail stage=wait_ibf_before_read_cfg\n");
        return false;
    }

    X86OutB(PS2_CMD_PORT, PS2_CMD_READ_CONFIG_BYTE);

    uint8_t ControllerConfig = 0;
    if (!ReadControllerDataByte(&ControllerConfig, PS2_DEFAULT_SPIN_COUNT))
    {
        // Some virtual controllers do not return the config byte reliably.
        // Fall back to enabling keyboard/mouse IRQ bits directly and continue.
        UsedFallbackConfig = true;
        ControllerConfig = static_cast<uint8_t>(PS2_CONFIG_ENABLE_KEYBOARD_INTERRUPT | PS2_CONFIG_ENABLE_MOUSE_INTERRUPT);
        ControllerConfig &= static_cast<uint8_t>(~PS2_CONFIG_DISABLE_MOUSE_CLOCK);
        MouseLogf("mouse_dbg: warn stage=read_cfg_timeout status=0x%x fallback_cfg=0x%x\n", X86InB(PS2_STATUS_PORT), ControllerConfig);
    }

    if (!UsedFallbackConfig)
    {
        ControllerConfig |= static_cast<uint8_t>(PS2_CONFIG_ENABLE_KEYBOARD_INTERRUPT);
        ControllerConfig &= static_cast<uint8_t>(~PS2_CONFIG_DISABLE_MOUSE_CLOCK);
    }

    uint8_t ControllerConfigDuringInit = static_cast<uint8_t>(ControllerConfig & static_cast<uint8_t>(~PS2_CONFIG_ENABLE_MOUSE_INTERRUPT));

    if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
    {
        InitFailureCode = MOUSE_INIT_FAIL_WAIT_IBF_BEFORE_WRITE_CFG_CMD;
        MouseLogf("mouse_dbg: init_fail stage=wait_ibf_before_write_cfg_cmd\n");
        return false;
    }

    X86OutB(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG_BYTE);

    if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
    {
        InitFailureCode = MOUSE_INIT_FAIL_WAIT_IBF_BEFORE_WRITE_CFG_DATA;
        MouseLogf("mouse_dbg: init_fail stage=wait_ibf_before_write_cfg_data\n");
        return false;
    }

    // Keep mouse IRQ disabled while sending mouse init commands so ACK bytes
    // are not consumed by the IRQ path before polling reads them.
    X86OutB(PS2_DATA_PORT, ControllerConfigDuringInit);

    DrainControllerOutputBuffer(32);

    bool DefaultsSet      = SendMouseCommand(PS2_MOUSE_CMD_SET_DEFAULTS, &DefaultsCommandResult, &DefaultsCommandResponse);
    bool StreamingEnabled = SendMouseCommand(PS2_MOUSE_CMD_ENABLE_STREAMING, &StreamingCommandResult, &StreamingCommandResponse);

    if (DefaultsSet && StreamingEnabled)
    {
        uint8_t ControllerConfigRuntime = static_cast<uint8_t>(ControllerConfigDuringInit | PS2_CONFIG_ENABLE_MOUSE_INTERRUPT);

        if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
        {
            InitFailureCode = MOUSE_INIT_FAIL_WAIT_IBF_BEFORE_WRITE_CFG_CMD;
            MouseLogf("mouse_dbg: init_fail stage=wait_ibf_before_write_cfg_cmd_runtime\n");
            return false;
        }

        X86OutB(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG_BYTE);

        if (!WaitForControllerInputBufferReady(PS2_DEFAULT_SPIN_COUNT))
        {
            InitFailureCode = MOUSE_INIT_FAIL_WAIT_IBF_BEFORE_WRITE_CFG_DATA;
            MouseLogf("mouse_dbg: init_fail stage=wait_ibf_before_write_cfg_data_runtime\n");
            return false;
        }

        X86OutB(PS2_DATA_PORT, ControllerConfigRuntime);
    }

    if (!DefaultsSet)
    {
        InitFailureCode = MOUSE_INIT_FAIL_SET_DEFAULTS;
    }
    else if (!StreamingEnabled)
    {
        InitFailureCode = MOUSE_INIT_FAIL_ENABLE_STREAMING;
    }
    else
    {
        InitFailureCode = MOUSE_INIT_OK;
    }
    MouseLogf("mouse_dbg: defaults=%u streaming=%u\n", DefaultsSet ? 1U : 0U, StreamingEnabled ? 1U : 0U);
    MouseLogf("mouse_dbg: defaults_result=%u defaults_resp=0x%x streaming_result=%u streaming_resp=0x%x\n", DefaultsCommandResult, DefaultsCommandResponse,
              StreamingCommandResult, StreamingCommandResponse);

    return DefaultsSet && StreamingEnabled;
}

void Mouse::HandleInterrupt()
{
    ++InterruptCount;

    uint8_t Status = X86InB(PS2_STATUS_PORT);

    if ((Status & PS2_STATUS_OUTPUT_BUFFER_FULL) == 0)
    {
        return;
    }

    uint8_t DataByte = X86InB(PS2_DATA_PORT);

    if ((Status & PS2_STATUS_MOUSE_DATA) == 0)
    {
        ++IgnoredIrqCount;
        if ((IgnoredIrqCount % MOUSE_LOG_IRQ_INTERVAL) == 1)
        {
            MouseLogf("mouse_dbg: irq_without_mouse_data status=0x%x byte=0x%x count=%lu\n", Status, DataByte, static_cast<unsigned long>(IgnoredIrqCount));
        }
        return;
    }

    if (PacketIndex == 0 && (DataByte & PS2_PACKET_ALWAYS_ONE) == 0)
    {
        ++DecodeFailureCount;
        if ((DecodeFailureCount % MOUSE_LOG_PACKET_INTERVAL) == 1)
        {
            MouseLogf("mouse_dbg: dropped_desync_byte=0x%x drops=%lu\n", DataByte, static_cast<unsigned long>(DecodeFailureCount));
        }
        return;
    }

    PendingPacket[PacketIndex++] = DataByte;

    if (PacketIndex < 3)
    {
        return;
    }

    ++PacketCount;
    PacketIndex      = 0;
    HasPendingPacket = true;
    DispatchEventInterrupt();
    HasPendingPacket = false;
}

bool Mouse::DispatchEventInterrupt()
{
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

    EventDevice* Device = EventManager->GetEventDeviceByOriginalDevice(this);
    if (Device == nullptr || Device->HandleIntrrupt == nullptr)
    {
        return false;
    }

    return Device->HandleIntrrupt(Device, this);
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
        bool SynQueued = EventManager->QueueInputEvent(Device, LINUX_EV_SYN, LINUX_SYN_REPORT, 0);
        if (!SynQueued)
        {
            MouseLogf("mouse_dbg: overflow_packet_syn_queue_failed\n");
        }
        return SynQueued;
    }

    int32_t DeltaX = static_cast<int32_t>(static_cast<int8_t>(Packet1));
    int32_t DeltaY = -static_cast<int32_t>(static_cast<int8_t>(Packet2));

    uint8_t CurrentButtonState = Packet0 & (PS2_PACKET_LEFT_BUTTON | PS2_PACKET_RIGHT_BUTTON | PS2_PACKET_MIDDLE_BUTTON);
    uint8_t ButtonChanges      = CurrentButtonState ^ MouseDevice->PreviousButtonState;
    MouseDevice->PreviousButtonState = CurrentButtonState;

    if (DeltaX == 0 && DeltaY == 0 && ButtonChanges == 0)
    {
        return true;
    }

    bool EventQueued = true;

    if (DeltaX != 0)
    {
        EventQueued = EventManager->QueueInputEvent(Device, LINUX_EV_REL, LINUX_REL_X, DeltaX) && EventQueued;
    }

    if (DeltaY != 0)
    {
        EventQueued = EventManager->QueueInputEvent(Device, LINUX_EV_REL, LINUX_REL_Y, DeltaY) && EventQueued;
    }

    if ((ButtonChanges & PS2_PACKET_LEFT_BUTTON) != 0)
    {
        int32_t LeftButtonValue = ((CurrentButtonState & PS2_PACKET_LEFT_BUTTON) != 0) ? 1 : 0;
        EventQueued = EventManager->QueueInputEvent(Device, LINUX_EV_KEY, LINUX_BTN_LEFT, LeftButtonValue) && EventQueued;
    }

    if ((ButtonChanges & PS2_PACKET_RIGHT_BUTTON) != 0)
    {
        int32_t RightButtonValue = ((CurrentButtonState & PS2_PACKET_RIGHT_BUTTON) != 0) ? 1 : 0;
        EventQueued = EventManager->QueueInputEvent(Device, LINUX_EV_KEY, LINUX_BTN_RIGHT, RightButtonValue) && EventQueued;
    }

    if ((ButtonChanges & PS2_PACKET_MIDDLE_BUTTON) != 0)
    {
        int32_t MiddleButtonValue = ((CurrentButtonState & PS2_PACKET_MIDDLE_BUTTON) != 0) ? 1 : 0;
        EventQueued = EventManager->QueueInputEvent(Device, LINUX_EV_KEY, LINUX_BTN_MIDDLE, MiddleButtonValue) && EventQueued;
    }

    bool SynQueued = EventManager->QueueInputEvent(Device, LINUX_EV_SYN, LINUX_SYN_REPORT, 0);
    if (!SynQueued || !EventQueued)
    {
        MouseLogf("mouse_dbg: queue_failed dx=%d dy=%d buttons=0x%x changes=0x%x syn=%u\n", static_cast<int>(DeltaX), static_cast<int>(DeltaY),
                  static_cast<unsigned int>(CurrentButtonState), static_cast<unsigned int>(ButtonChanges), SynQueued ? 1U : 0U);
    }

    return SynQueued && EventQueued;
}
