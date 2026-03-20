/**
 * File: DeviceManager.hpp
 * Author: Marwan Mostafa
 * Description: Device manager interface declarations.
 */

#pragma once

#include <stdint.h>

class TTY;

typedef struct
{
    uint8_t  Bus;
    uint8_t  Device;
    uint8_t  Function;
    uint16_t VendorId;
    uint16_t DeviceId;
    uint8_t  ClassCode;
    uint8_t  SubClass;
    uint8_t  ProgrammingInterface;
    uint8_t  RevisionId;
} PciDeviceInfo;

class DeviceManager
{
private:
    static constexpr uint32_t MAX_PCI_DEVICES = 256;
    PciDeviceInfo             PciDevices[MAX_PCI_DEVICES];
    uint32_t                  PciDeviceCount;

public:
    DeviceManager();
    void Initialize();
    void EnumeratePCI();
    void PrintPCI(TTY* Terminal) const;
    uint32_t GetPCIDeviceCount() const;
    bool GetPCIDeviceInfo(uint32_t Index, PciDeviceInfo* Info) const;
};