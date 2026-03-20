/**
 * File: PartitionManager.hpp
 * Author: Marwan Mostafa
 * Description: Disk partition discovery interface declarations.
 */

#pragma once

#include <stdint.h>

class DeviceManager;
class VirtualFileSystem;

typedef struct
{
    uint64_t StartLBA;
    uint64_t SectorCount;
    uint32_t PartitionIndex;
    char     DevicePath[16];
} RootFileSystemPartitionInfo;

class PartitionManager
{
private:
    static constexpr uint32_t MAX_PARTITION_DEVICE_COUNT = 64;

    RootFileSystemPartitionInfo CachedPartitions[MAX_PARTITION_DEVICE_COUNT];
    uint32_t                    CachedPartitionCount;

public:
    PartitionManager();
    bool     RefreshPartitionCache(const DeviceManager* DeviceManagerInstance);
    bool     GetPartitionByDevicePath(const char* DevicePath, RootFileSystemPartitionInfo* PartitionInfo) const;
    bool     GetCachedPartitionByIndex(uint32_t Index, RootFileSystemPartitionInfo* PartitionInfo) const;
    uint32_t GetCachedPartitionCount() const;
    bool     EnumeratePartitions(const DeviceManager* DeviceManagerInstance, RootFileSystemPartitionInfo* PartitionInfos, uint32_t MaxPartitionCount, uint32_t* PartitionCount);
    bool     RegisterPartitionDevices(const DeviceManager* DeviceManagerInstance, VirtualFileSystem* VFS);
    bool     LocateRootFileSystemPartition(const DeviceManager* DeviceManagerInstance, RootFileSystemPartitionInfo* PartitionInfo);
};
