/**
 * File: EventDeviceManager.hpp
 * Description: Event device manager declarations.
 */

#pragma once

#include <stdint.h>

struct File;
struct FileOperations;
class LogicLayer;
struct Process;
class VirtualAddressSpace;

struct LinuxInputEvent
{
    int64_t  Seconds;
    int64_t  Microseconds;
    uint16_t Type;
    uint16_t Code;
    int32_t  Value;
};

struct EventDevice;
typedef bool (*EventDeviceInterruptHandler)(EventDevice* Device, void* OriginalDevice);

enum EventDeviceKind
{
    EVENT_DEVICE_KIND_GENERIC  = 0,
    EVENT_DEVICE_KIND_KEYBOARD = 1,
    EVENT_DEVICE_KIND_MOUSE    = 2,
};

struct EventDevice
{
    static constexpr uint32_t MAX_WAITING_PROCESS_IDS = 64;
    static constexpr uint32_t MAX_EVENT_DEVICE_PATH   = 64;
    static constexpr uint32_t MAX_PENDING_EVENTS      = 1024;

    void*           OriginalDevice;
    char            Path[MAX_EVENT_DEVICE_PATH];
    uint8_t         WaitingProcessIds[MAX_WAITING_PROCESS_IDS];
    uint32_t        WaitingProcessCount;
    LinuxInputEvent PendingEvents[MAX_PENDING_EVENTS];
    uint32_t        PendingEventHead;
    uint32_t        PendingEventTail;
    uint32_t        PendingEventCount;
    EventDeviceInterruptHandler HandleIntrrupt;
    EventDeviceKind Kind;
    bool            InUse;
    uint8_t         LastWokenProcessId;
    bool            OverflowedSinceLastSync;

    FileOperations* GetFileOperations();

    static int64_t ReadFileOperation(File* OpenFile, void* Buffer, uint64_t Count);
    static int64_t WriteFileOperation(File* OpenFile, const void* Buffer, uint64_t Count);
    static int64_t SeekFileOperation(File* OpenFile, int64_t Offset, int32_t Whence);
    static int64_t MemoryMapFileOperation(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address);
    static int64_t PollFileOperation(File* OpenFile, uint32_t RequestedEvents, uint32_t* ReturnedEvents, LogicLayer* Logic, Process* RunningProcess);
    static int64_t IoctlFileOperation(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic, Process* RunningProcess);
};

class EventDeviceManager
{
private:
    static constexpr uint32_t MAX_EVENT_DEVICES = 32;

    EventDevice EventDevices[MAX_EVENT_DEVICES];
    uint32_t    EventDeviceCount;

    static bool CopyPath(char* Destination, uint32_t DestinationSize, const char* Source);
    int32_t     FindEventDeviceIndexByPath(const char* Path) const;

public:
    EventDeviceManager();

    void        Reset();
    EventDevice* CreateEventDevice(void* OriginalDevice, const char* Path, EventDeviceInterruptHandler InterruptHandler, EventDeviceKind Kind = EVENT_DEVICE_KIND_GENERIC);
    bool        RemoveEventDevice(const char* Path);
    EventDevice* GetEventDevice(const char* Path);
    EventDevice* GetEventDeviceByOriginalDevice(void* OriginalDevice);
    bool        AddWaitingProcess(EventDevice* Device, uint8_t ProcessId);
    bool        RemoveWaitingProcess(EventDevice* Device, uint8_t ProcessId);
    bool        QueueInputEvent(const char* Path, uint16_t Type, uint16_t Code, int32_t Value);
    bool        QueueInputEvent(EventDevice* Device, uint16_t Type, uint16_t Code, int32_t Value);
    uint32_t    GetEventDeviceCount() const;
    bool        GetEventDevice(uint32_t Index, EventDevice* Device) const;
};
