/**
 * File: IDEController.cpp
 * Author: Marwan Mostafa
 * Description: IDE controller driver implementation for LBA block I/O.
 */

#include "IDEController.hpp"

#include <Arch/x86.hpp>

namespace
{
constexpr uint8_t ATA_STATUS_ERROR           = 0x01;
constexpr uint8_t ATA_STATUS_DATA_REQUEST    = 0x08;
constexpr uint8_t ATA_STATUS_DEVICE_FAULT    = 0x20;
constexpr uint8_t ATA_STATUS_BUSY            = 0x80;

constexpr uint8_t ATA_COMMAND_READ_SECTORS   = 0x20;
constexpr uint8_t ATA_COMMAND_WRITE_SECTORS  = 0x30;
constexpr uint8_t ATA_COMMAND_CACHE_FLUSH    = 0xE7;

constexpr uint8_t ATA_DEVICE_LBA_MASTER_BASE = 0xE0;

static inline void IOWait()
{
    X86OutB(0x80, 0);
}

static inline bool LBAFits28Bit(uint32_t LBA)
{
    return (LBA & 0xF0000000u) == 0;
}
} // namespace

IDEController::IDEController(uint16_t PrimaryIoBase, uint16_t PrimaryControlBase) : PrimaryIoBase(PrimaryIoBase), PrimaryControlBase(PrimaryControlBase), Initialized(false)
{
}

bool IDEController::WaitForControllerReady(bool RequireDataRequest) const
{
    for (uint32_t Attempts = 0; Attempts < 1000000; ++Attempts)
    {
        uint8_t Status = X86InB(static_cast<uint16_t>(PrimaryIoBase + 7));

        if ((Status & ATA_STATUS_BUSY) != 0)
        {
            continue;
        }

        if ((Status & ATA_STATUS_ERROR) != 0 || (Status & ATA_STATUS_DEVICE_FAULT) != 0)
        {
            return false;
        }

        if (!RequireDataRequest)
        {
            return true;
        }

        if ((Status & ATA_STATUS_DATA_REQUEST) != 0)
        {
            return true;
        }
    }

    return false;
}

bool IDEController::Initialize()
{
    X86OutB(PrimaryControlBase, 0x04);
    IOWait();
    X86OutB(PrimaryControlBase, 0x00);

    for (uint32_t Attempts = 0; Attempts < 100000; ++Attempts)
    {
        uint8_t Status = X86InB(static_cast<uint16_t>(PrimaryIoBase + 7));

        if (Status == 0xFF)
        {
            Initialized = false;
            return false;
        }

        if ((Status & ATA_STATUS_BUSY) == 0)
        {
            Initialized = true;
            return true;
        }
    }

    Initialized = false;
    return false;
}

bool IDEController::IsInitialized() const
{
    return Initialized;
}

bool IDEController::HandleInterrupt() const
{
    if (!Initialized)
    {
        return false;
    }

    volatile uint8_t Status = X86InB(static_cast<uint16_t>(PrimaryIoBase + 7));
    (void) Status;
    return true;
}

bool IDEController::ReadBlock(uint32_t LBA, void* Buffer) const
{
    if (!Initialized || Buffer == nullptr || !LBAFits28Bit(LBA))
    {
        return false;
    }

    uint16_t* Destination = static_cast<uint16_t*>(Buffer);

    X86OutB(PrimaryControlBase, 0x02);
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 6), static_cast<uint8_t>(ATA_DEVICE_LBA_MASTER_BASE | ((LBA >> 24) & 0x0F)));
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 1), 0x00);
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 2), 0x01);
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 3), static_cast<uint8_t>(LBA & 0xFF));
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 4), static_cast<uint8_t>((LBA >> 8) & 0xFF));
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 5), static_cast<uint8_t>((LBA >> 16) & 0xFF));
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 7), ATA_COMMAND_READ_SECTORS);

    if (!WaitForControllerReady(true))
    {
        return false;
    }

    for (uint32_t WordIndex = 0; WordIndex < (BLOCK_SIZE_BYTES / 2); ++WordIndex)
    {
        Destination[WordIndex] = X86InW(static_cast<uint16_t>(PrimaryIoBase));
    }

    return true;
}

bool IDEController::WriteBlock(uint32_t LBA, const void* Buffer) const
{
    if (!Initialized || Buffer == nullptr || !LBAFits28Bit(LBA))
    {
        return false;
    }

    const uint16_t* Source = static_cast<const uint16_t*>(Buffer);

    X86OutB(PrimaryControlBase, 0x02);
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 6), static_cast<uint8_t>(ATA_DEVICE_LBA_MASTER_BASE | ((LBA >> 24) & 0x0F)));
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 1), 0x00);
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 2), 0x01);
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 3), static_cast<uint8_t>(LBA & 0xFF));
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 4), static_cast<uint8_t>((LBA >> 8) & 0xFF));
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 5), static_cast<uint8_t>((LBA >> 16) & 0xFF));
    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 7), ATA_COMMAND_WRITE_SECTORS);

    if (!WaitForControllerReady(true))
    {
        return false;
    }

    for (uint32_t WordIndex = 0; WordIndex < (BLOCK_SIZE_BYTES / 2); ++WordIndex)
    {
        X86OutW(static_cast<uint16_t>(PrimaryIoBase), Source[WordIndex]);
    }

    X86OutB(static_cast<uint16_t>(PrimaryIoBase + 7), ATA_COMMAND_CACHE_FLUSH);
    return WaitForControllerReady(false);
}

uint32_t IDEController::GetBlockSizeBytes() const
{
    return BLOCK_SIZE_BYTES;
}
