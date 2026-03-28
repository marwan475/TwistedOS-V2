/**
 * File: DeviceManager.hpp
 * Author: Marwan Mostafa
 * Description: Device manager interface declarations.
 */

#pragma once

#include <stdint.h>

class TTY;
class IDEController;
class VirtioRandom;
class PhysicalMemoryManager;
struct FileOperations;

typedef struct
{
    const char*     FileName;
    void*           DeviceData;
    FileOperations* FileOps;
} DeviceNodeRegistration;

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
    static constexpr uint16_t QEMU_IDE_VENDOR_ID = 0x8086;
    static constexpr uint16_t QEMU_IDE_DEVICE_ID = 0x7010;
    static constexpr uint16_t VIRTIO_VENDOR_ID   = 0x1AF4;
    static constexpr uint16_t VIRTIO_RNG_DEVICE_ID_LEGACY = 0x1005;
    static constexpr uint16_t VIRTIO_RNG_DEVICE_ID_MODERN = 0x1044;
    static constexpr uint32_t MAX_PCI_DEVICES    = 256;
    static constexpr uint32_t MAX_DEVICE_NODES   = 32;
    PciDeviceInfo             PciDevices[MAX_PCI_DEVICES];
    uint32_t                  PciDeviceCount;
    DeviceNodeRegistration    DeviceNodes[MAX_DEVICE_NODES];
    uint32_t                  DeviceNodeCount;
    IDEController*            PrimaryIDEController;
    VirtioRandom*             PrimaryVirtioRandom;
    PhysicalMemoryManager*    PhysicalMemory;
    TTY*                      LogTerminal;

    void InitializeIDEControllerForDevice(const PciDeviceInfo& Device);
    void InitializeVirtioRandomForDevice(const PciDeviceInfo& Device);

public:
    DeviceManager();
    ~DeviceManager();
    void           Initialize(TTY* Terminal, PhysicalMemoryManager* PMM);
    void           EnumeratePCI();
    void           PrintPCI(TTY* Terminal) const;
    uint32_t       GetPCIDeviceCount() const;
    bool           GetPCIDeviceInfo(uint32_t Index, PciDeviceInfo* Info) const;
    bool           RegisterDeviceNode(const char* FileName, void* DeviceData, FileOperations* FileOps);
    uint32_t       GetRegisteredDeviceCount() const;
    bool           GetRegisteredDevice(uint32_t Index, DeviceNodeRegistration* Registration) const;
    IDEController* GetDiskController() const;
    TTY*           GetLogTerminal() const;
    bool           ReadBlock(uint32_t LBA, void* Buffer) const;
    bool           WriteBlock(uint32_t LBA, const void* Buffer) const;
    IDEController* GetPrimaryIDEController() const;
    VirtioRandom*  GetVirtioRandom() const;
};