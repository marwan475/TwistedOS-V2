/**
 * File: PartitionManager.hpp
 * Author: Marwan Mostafa
 * Description: Disk partition discovery interface declarations.
 */

#pragma once

#include <stdint.h>

class DeviceManager;

typedef struct
{
    uint64_t StartLBA;
    uint64_t SectorCount;
    uint32_t PartitionIndex;
} RootFileSystemPartitionInfo;

class PartitionManager
{
public:
    static bool LocateRootFileSystemPartition(const DeviceManager* DeviceManagerInstance, RootFileSystemPartitionInfo* PartitionInfo);
};
