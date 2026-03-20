/**
 * File: DeviceManager.cpp
 * Author: Marwan Mostafa
 * Description: Device manager interface implementation.
 */

#include "DeviceManager.hpp"

#include "TTY.hpp"

#include <Arch/x86.hpp>

namespace
{
bool ReadPciConfigDword(uint8_t Bus, uint8_t Device, uint8_t Function, uint8_t RegisterOffset, uint32_t* Value)
{
	return X86ReadPCIConfigDword(Bus, Device, Function, RegisterOffset, Value);
}

bool ReadPciConfigWord(uint8_t Bus, uint8_t Device, uint8_t Function, uint8_t RegisterOffset, uint16_t* Value)
{
	if (Value == nullptr)
	{
		return false;
	}

	uint32_t DwordValue = 0;
	if (!ReadPciConfigDword(Bus, Device, Function, RegisterOffset, &DwordValue))
	{
		return false;
	}

	*Value = static_cast<uint16_t>((DwordValue >> ((RegisterOffset & 0x2u) * 8)) & 0xFFFFu);
	return true;
}

bool ReadPciConfigByte(uint8_t Bus, uint8_t Device, uint8_t Function, uint8_t RegisterOffset, uint8_t* Value)
{
	if (Value == nullptr)
	{
		return false;
	}

	uint32_t DwordValue = 0;
	if (!ReadPciConfigDword(Bus, Device, Function, RegisterOffset, &DwordValue))
	{
		return false;
	}

	*Value = static_cast<uint8_t>((DwordValue >> ((RegisterOffset & 0x3u) * 8)) & 0xFFu);
	return true;
}
} // namespace

/**
 * Function: DeviceManager::DeviceManager
 * Description: Constructs a device manager instance.
 * Parameters:
 *   None
 * Returns:
 *   DeviceManager - Constructed device manager instance.
 */
DeviceManager::DeviceManager()
    : PciDevices{}, PciDeviceCount(0)
{
}

/**
 * Function: DeviceManager::Initialize
 * Description: Initializes device manager state and resources.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void DeviceManager::Initialize()
{
	EnumeratePCI();
}

void DeviceManager::EnumeratePCI()
{
	PciDeviceCount = 0;

	for (uint16_t Bus = 0; Bus < 256; ++Bus)
	{
		for (uint8_t Device = 0; Device < 32; ++Device)
		{
			uint16_t VendorId = 0;
			if (!ReadPciConfigWord(static_cast<uint8_t>(Bus), Device, 0, 0x00, &VendorId) || VendorId == 0xFFFF)
			{
				continue;
			}

			uint8_t HeaderType    = 0;
			uint8_t FunctionCount = 1;
			if (ReadPciConfigByte(static_cast<uint8_t>(Bus), Device, 0, 0x0E, &HeaderType) && (HeaderType & 0x80) != 0)
			{
				FunctionCount = 8;
			}

			for (uint8_t Function = 0; Function < FunctionCount; ++Function)
			{
				if (PciDeviceCount >= MAX_PCI_DEVICES)
				{
					return;
				}

				PciDeviceInfo Info = {};
				if (!ReadPciConfigWord(static_cast<uint8_t>(Bus), Device, Function, 0x00, &Info.VendorId) || Info.VendorId == 0xFFFF)
				{
					continue;
				}

				Info.Bus      = static_cast<uint8_t>(Bus);
				Info.Device   = Device;
				Info.Function = Function;

				if (!ReadPciConfigWord(static_cast<uint8_t>(Bus), Device, Function, 0x02, &Info.DeviceId)
					|| !ReadPciConfigByte(static_cast<uint8_t>(Bus), Device, Function, 0x0B, &Info.ClassCode)
					|| !ReadPciConfigByte(static_cast<uint8_t>(Bus), Device, Function, 0x0A, &Info.SubClass)
					|| !ReadPciConfigByte(static_cast<uint8_t>(Bus), Device, Function, 0x09, &Info.ProgrammingInterface)
					|| !ReadPciConfigByte(static_cast<uint8_t>(Bus), Device, Function, 0x08, &Info.RevisionId))
				{
					continue;
				}

				PciDevices[PciDeviceCount++] = Info;
			}
		}
	}
}

/**
 * Function: DeviceManager::PrintPCI
 * Description: Enumerates PCI devices and prints each discovered function to the provided terminal.
 * Parameters:
 *   TTY* Terminal - Output terminal used to print discovered PCI devices.
 * Returns:
 *   void - No return value.
 */
void DeviceManager::PrintPCI(TTY* Terminal) const
{
    if (Terminal == nullptr)
    {
        return;
    }

    Terminal->printf_("PCI devices: %u\n", PciDeviceCount);

    for (uint32_t Index = 0; Index < PciDeviceCount; ++Index)
    {
        const PciDeviceInfo& Device = PciDevices[Index];
        Terminal->printf_("PCI %u:%u.%u vendor=0x%x device=0x%x class=0x%x subclass=0x%x progif=0x%x rev=0x%x\n", Device.Bus, Device.Device, Device.Function,
                          Device.VendorId, Device.DeviceId, Device.ClassCode, Device.SubClass, Device.ProgrammingInterface, Device.RevisionId);
    }
}

uint32_t DeviceManager::GetPCIDeviceCount() const
{
    return PciDeviceCount;
}

bool DeviceManager::GetPCIDeviceInfo(uint32_t Index, PciDeviceInfo* Info) const
{
    if (Info == nullptr || Index >= PciDeviceCount)
    {
        return false;
    }

    *Info = PciDevices[Index];
    return true;
}