/**
 * File: IDEController.hpp
 * Author: Marwan Mostafa
 * Description: IDE controller driver declarations for LBA block I/O.
 */

#pragma once

#include <stdint.h>

class IDEController
{
private:
    static constexpr uint32_t BLOCK_SIZE_BYTES = 512;

    uint16_t PrimaryIoBase;
    uint16_t PrimaryControlBase;
    bool     Initialized;

    bool WaitForControllerReady(bool RequireDataRequest) const;

public:
    IDEController(uint16_t PrimaryIoBase, uint16_t PrimaryControlBase);

    bool Initialize();
    bool IsInitialized() const;
    bool HandleInterrupt() const;
    bool ReadBlock(uint32_t LBA, void* Buffer) const;
    bool WriteBlock(uint32_t LBA, const void* Buffer) const;
    uint32_t GetBlockSizeBytes() const;
};
