/**
 * File: EventDeviceManager.cpp
 * Description: Event device manager implementation.
 */

#include "EventDeviceManager.hpp"

#include <Layers/Dispatcher.hpp>
#include <Layers/Logic/LogicLayer.hpp>
#include <Layers/Logic/ProcessManager.hpp>
#include <Layers/Logic/VirtualFileSystem.hpp>
#include <Layers/Resource/ResourceLayer.hpp>

#include <CommonUtils.hpp>

namespace
{
constexpr int64_t LINUX_ERR_EFAULT = -14;
constexpr int64_t LINUX_ERR_EINVAL = -22;
constexpr int64_t LINUX_ERR_ENOSYS = -38;
constexpr int64_t LINUX_ERR_ENOTTY = -25;
constexpr int64_t LINUX_ERR_EAGAIN = -11;
constexpr int64_t LINUX_ERR_EINTR  = -4;

constexpr uint64_t LINUX_O_NONBLOCK = 0x800;

constexpr uint16_t LINUX_POLLIN  = 0x0001;
constexpr uint32_t EVENT_DEVICE_DEBUG_LOG_INTERVAL = 128;
constexpr uint16_t LINUX_POLLOUT = 0x0004;

constexpr uint64_t LINUX_IOCTL_FIONREAD      = 0x541B;
constexpr uint64_t LINUX_IOCTL_EVIOCGVERSION = 0x80044501;
constexpr uint64_t LINUX_IOCTL_EVIOCGID      = 0x80084502;
constexpr uint64_t LINUX_IOCTL_EVIOCGRAB     = 0x40044590;

constexpr uint32_t LINUX_EV_VERSION = 0x010001;
constexpr uint16_t LINUX_BUS_VIRTUAL = 0x06;

constexpr uint16_t LINUX_EV_SYN = 0x00;
constexpr uint16_t LINUX_EV_KEY = 0x01;
constexpr uint16_t LINUX_EV_REL = 0x02;
constexpr uint16_t LINUX_EV_MSC = 0x04;
constexpr uint16_t LINUX_MSC_SCAN = 0x04;

constexpr uint16_t LINUX_REL_X = 0x00;
constexpr uint16_t LINUX_REL_Y = 0x01;

constexpr uint16_t LINUX_BTN_LEFT   = 0x110;
constexpr uint16_t LINUX_BTN_RIGHT  = 0x111;
constexpr uint16_t LINUX_BTN_MIDDLE = 0x112;

constexpr uint32_t LINUX_IOC_NRBITS   = 8;
constexpr uint32_t LINUX_IOC_TYPEBITS = 8;
constexpr uint32_t LINUX_IOC_SIZEBITS = 14;
constexpr uint32_t LINUX_IOC_DIRBITS  = 2;

constexpr uint32_t LINUX_IOC_NRSHIFT   = 0;
constexpr uint32_t LINUX_IOC_TYPESHIFT = LINUX_IOC_NRSHIFT + LINUX_IOC_NRBITS;
constexpr uint32_t LINUX_IOC_SIZESHIFT = LINUX_IOC_TYPESHIFT + LINUX_IOC_TYPEBITS;
constexpr uint32_t LINUX_IOC_DIRSHIFT  = LINUX_IOC_SIZESHIFT + LINUX_IOC_SIZEBITS;

constexpr uint32_t LINUX_IOC_READ = 2;

struct LinuxInputId
{
    uint16_t BusType;
    uint16_t Vendor;
    uint16_t Product;
    uint16_t Version;
};

uint32_t IoctlNumber(uint64_t Request)
{
    return static_cast<uint32_t>((Request >> LINUX_IOC_NRSHIFT) & ((1U << LINUX_IOC_NRBITS) - 1));
}

uint32_t IoctlType(uint64_t Request)
{
    return static_cast<uint32_t>((Request >> LINUX_IOC_TYPESHIFT) & ((1U << LINUX_IOC_TYPEBITS) - 1));
}

uint32_t IoctlSize(uint64_t Request)
{
    return static_cast<uint32_t>((Request >> LINUX_IOC_SIZESHIFT) & ((1U << LINUX_IOC_SIZEBITS) - 1));
}

uint32_t IoctlDirection(uint64_t Request)
{
    return static_cast<uint32_t>((Request >> LINUX_IOC_DIRSHIFT) & ((1U << LINUX_IOC_DIRBITS) - 1));
}

bool IsEventIoctlReadRequest(uint64_t Request, uint32_t Number)
{
    return IoctlType(Request) == static_cast<uint32_t>('E') && IoctlNumber(Request) == Number && (IoctlDirection(Request) & LINUX_IOC_READ) != 0;
}

void SetBit(uint8_t* Buffer, uint32_t Bit)
{
    if (Buffer == nullptr)
    {
        return;
    }

    Buffer[Bit / 8] = static_cast<uint8_t>(Buffer[Bit / 8] | (1U << (Bit % 8)));
}

bool IsSameString(const char* A, const char* B)
{
    if (A == nullptr || B == nullptr)
    {
        return false;
    }

    size_t Index = 0;
    while (A[Index] != '\0' && B[Index] != '\0')
    {
        if (A[Index] != B[Index])
        {
            return false;
        }

        ++Index;
    }

    return A[Index] == B[Index];
}

bool CopyCStringBounded(char* Destination, uint32_t DestinationSize, const char* Source)
{
    if (Destination == nullptr || DestinationSize == 0 || Source == nullptr)
    {
        return false;
    }

    uint32_t Index = 0;
    for (; Source[Index] != '\0'; ++Index)
    {
        if (Index + 1 >= DestinationSize)
        {
            Destination[0] = '\0';
            return false;
        }

        Destination[Index] = Source[Index];
    }

    Destination[Index] = '\0';
    return true;
}

const char* ResolveEventDeviceDisplayName(const EventDevice* Device)
{
    if (Device == nullptr)
    {
        return "TwistedOS Event Device";
    }

    switch (Device->Kind)
    {
        case EVENT_DEVICE_KIND_KEYBOARD:
            return "TwistedOS PS/2 Keyboard";
        case EVENT_DEVICE_KIND_MOUSE:
            return "TwistedOS PS/2 Mouse";
        default:
            break;
    }

    return "TwistedOS Event Device";
}

EventDeviceManager* GetGlobalEventDeviceManager()
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

    return ActiveResourceLayer->GetEventDeviceManager();
}

TTY* GetEventDeviceLogTTY()
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

bool IsMouseEventDevice(const EventDevice* Device)
{
    return Device != nullptr && Device->Kind == EVENT_DEVICE_KIND_MOUSE;
}

bool ResolveRuntimeContext(LogicLayer** OutLogic, Process** OutRunningProcess)
{
    if (OutLogic == nullptr || OutRunningProcess == nullptr)
    {
        return false;
    }

    *OutLogic          = nullptr;
    *OutRunningProcess = nullptr;

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        return false;
    }

    LogicLayer* ActiveLogicLayer = ActiveDispatcher->GetLogicLayer();
    if (ActiveLogicLayer == nullptr)
    {
        return false;
    }

    ProcessManager* ProcessManagerInstance = ActiveLogicLayer->GetProcessManager();
    if (ProcessManagerInstance == nullptr)
    {
        return false;
    }

    Process* RunningProcess = ProcessManagerInstance->GetRunningProcess();
    if (RunningProcess == nullptr)
    {
        return false;
    }

    *OutLogic          = ActiveLogicLayer;
    *OutRunningProcess = RunningProcess;
    return true;
}
} // namespace

EventDeviceManager::EventDeviceManager() : EventDevices{}, EventDeviceCount(0)
{
}

void EventDeviceManager::Reset()
{
    kmemset(EventDevices, 0, sizeof(EventDevices));
    EventDeviceCount = 0;
}

EventDevice* EventDeviceManager::CreateEventDevice(void* OriginalDevice, const char* Path, EventDeviceInterruptHandler InterruptHandler, EventDeviceKind Kind)
{
    if (OriginalDevice == nullptr || Path == nullptr || Path[0] == '\0')
    {
        return nullptr;
    }

    const int32_t ExistingIndex = FindEventDeviceIndexByPath(Path);
    if (ExistingIndex >= 0)
    {
        return &EventDevices[ExistingIndex];
    }

    if (EventDeviceCount >= MAX_EVENT_DEVICES)
    {
        return nullptr;
    }

    for (uint32_t Index = 0; Index < MAX_EVENT_DEVICES; ++Index)
    {
        EventDevice& Device = EventDevices[Index];
        if (Device.InUse)
        {
            continue;
        }

        Device = {};
        Device.OriginalDevice    = OriginalDevice;
        Device.WaitingProcessCount = 0;
        Device.PendingEventHead    = 0;
        Device.PendingEventTail    = 0;
        Device.PendingEventCount   = 0;
        Device.HandleIntrrupt      = InterruptHandler;
        Device.Kind                = Kind;
        Device.InUse             = true;

        if (!CopyPath(Device.Path, EventDevice::MAX_EVENT_DEVICE_PATH, Path))
        {
            Device = {};
            return nullptr;
        }

        ++EventDeviceCount;
        return &Device;
    }

    return nullptr;
}

bool EventDeviceManager::RemoveEventDevice(const char* Path)
{
    const int32_t Index = FindEventDeviceIndexByPath(Path);
    if (Index < 0)
    {
        return false;
    }

    EventDevices[Index] = {};
    if (EventDeviceCount > 0)
    {
        --EventDeviceCount;
    }

    return true;
}

EventDevice* EventDeviceManager::GetEventDevice(const char* Path)
{
    const int32_t Index = FindEventDeviceIndexByPath(Path);
    if (Index < 0)
    {
        return nullptr;
    }

    return &EventDevices[Index];
}

EventDevice* EventDeviceManager::GetEventDeviceByOriginalDevice(void* OriginalDevice)
{
    if (OriginalDevice == nullptr)
    {
        return nullptr;
    }

    for (uint32_t Index = 0; Index < MAX_EVENT_DEVICES; ++Index)
    {
        EventDevice& Device = EventDevices[Index];
        if (!Device.InUse)
        {
            continue;
        }

        if (Device.OriginalDevice == OriginalDevice)
        {
            return &Device;
        }
    }

    return nullptr;
}

bool EventDeviceManager::AddWaitingProcess(EventDevice* Device, uint8_t ProcessId)
{
    if (Device == nullptr)
    {
        return false;
    }

    for (uint32_t Index = 0; Index < Device->WaitingProcessCount; ++Index)
    {
        if (Device->WaitingProcessIds[Index] == ProcessId)
        {
            return true;
        }
    }

    if (Device->WaitingProcessCount >= EventDevice::MAX_WAITING_PROCESS_IDS)
    {
        return false;
    }

    Device->WaitingProcessIds[Device->WaitingProcessCount++] = ProcessId;
    return true;
}

bool EventDeviceManager::RemoveWaitingProcess(EventDevice* Device, uint8_t ProcessId)
{
    if (Device == nullptr)
    {
        return false;
    }

    for (uint32_t Index = 0; Index < Device->WaitingProcessCount; ++Index)
    {
        if (Device->WaitingProcessIds[Index] != ProcessId)
        {
            continue;
        }

        for (uint32_t ShiftIndex = Index + 1; ShiftIndex < Device->WaitingProcessCount; ++ShiftIndex)
        {
            Device->WaitingProcessIds[ShiftIndex - 1] = Device->WaitingProcessIds[ShiftIndex];
        }

        --Device->WaitingProcessCount;
        return true;
    }

    return false;
}

bool EventDeviceManager::QueueInputEvent(const char* Path, uint16_t Type, uint16_t Code, int32_t Value)
{
    EventDevice* Device = GetEventDevice(Path);
    return QueueInputEvent(Device, Type, Code, Value);
}

bool EventDeviceManager::QueueInputEvent(EventDevice* Device, uint16_t Type, uint16_t Code, int32_t Value)
{
    if (Device == nullptr)
    {
        return false;
    }

    if (Device->PendingEventCount >= EventDevice::MAX_PENDING_EVENTS)
    {
        Device->PendingEventHead = (Device->PendingEventHead + 1) % EventDevice::MAX_PENDING_EVENTS;
        --Device->PendingEventCount;
    }

    LinuxInputEvent Event = {};
    Event.Seconds         = 0;
    Event.Microseconds    = 0;
    Event.Type            = Type;
    Event.Code            = Code;
    Event.Value           = Value;

    Device->PendingEvents[Device->PendingEventTail] = Event;
    Device->PendingEventTail                         = (Device->PendingEventTail + 1) % EventDevice::MAX_PENDING_EVENTS;
    ++Device->PendingEventCount;

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher != nullptr)
    {
        LogicLayer* ActiveLogicLayer = ActiveDispatcher->GetLogicLayer();
        if (ActiveLogicLayer != nullptr)
        {
            for (uint32_t Index = 0; Index < Device->WaitingProcessCount; ++Index)
            {
                ActiveLogicLayer->UnblockProcess(Device->WaitingProcessIds[Index]);
            }
        }
    }

    Device->WaitingProcessCount = 0;
    return true;
}

uint32_t EventDeviceManager::GetEventDeviceCount() const
{
    return EventDeviceCount;
}

bool EventDeviceManager::GetEventDevice(uint32_t Index, EventDevice* Device) const
{
    if (Device == nullptr || Index >= MAX_EVENT_DEVICES || !EventDevices[Index].InUse)
    {
        return false;
    }

    *Device = EventDevices[Index];
    return true;
}

bool EventDeviceManager::CopyPath(char* Destination, uint32_t DestinationSize, const char* Source)
{
    if (Destination == nullptr || DestinationSize == 0 || Source == nullptr)
    {
        return false;
    }

    uint32_t Index = 0;
    for (; Source[Index] != '\0'; ++Index)
    {
        if (Index + 1 >= DestinationSize)
        {
            Destination[0] = '\0';
            return false;
        }

        Destination[Index] = Source[Index];
    }

    Destination[Index] = '\0';
    return true;
}

int32_t EventDeviceManager::FindEventDeviceIndexByPath(const char* Path) const
{
    if (Path == nullptr || Path[0] == '\0')
    {
        return -1;
    }

    for (uint32_t Index = 0; Index < MAX_EVENT_DEVICES; ++Index)
    {
        const EventDevice& Device = EventDevices[Index];
        if (!Device.InUse)
        {
            continue;
        }

        if (IsSameString(Device.Path, Path))
        {
            return static_cast<int32_t>(Index);
        }
    }

    return -1;
}

FileOperations* EventDevice::GetFileOperations()
{
    static FileOperations EventDeviceFileOperations = {
        &EventDevice::ReadFileOperation,
        &EventDevice::WriteFileOperation,
        &EventDevice::SeekFileOperation,
        &EventDevice::MemoryMapFileOperation,
        &EventDevice::PollFileOperation,
        &EventDevice::IoctlFileOperation,
    };

    return &EventDeviceFileOperations;
}

int64_t EventDevice::ReadFileOperation(File* OpenFile, void* Buffer, uint64_t Count)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr || (Buffer == nullptr && Count != 0))
    {
        return LINUX_ERR_EFAULT;
    }

    EventDevice* Device = reinterpret_cast<EventDevice*>(OpenFile->Node->NodeData);
    if (Device == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Count == 0)
    {
        return 0;
    }

    if (Count < sizeof(LinuxInputEvent))
    {
        return LINUX_ERR_EINVAL;
    }

    EventDeviceManager* Manager = GetGlobalEventDeviceManager();
    if (Manager == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    bool IsNonBlocking = (OpenFile->OpenFlags & LINUX_O_NONBLOCK) != 0;

    LogicLayer* ActiveLogicLayer = nullptr;
    Process*    RunningProcess    = nullptr;
    if (!ResolveRuntimeContext(&ActiveLogicLayer, &RunningProcess))
    {
        return LINUX_ERR_EFAULT;
    }

    while (Device->PendingEventCount == 0)
    {
        if (IsNonBlocking)
        {
            return LINUX_ERR_EAGAIN;
        }

        Manager->AddWaitingProcess(Device, RunningProcess->Id);
        ActiveLogicLayer->SleepProcess(RunningProcess->Id, 1);

        if (RunningProcess->InterruptedBySignal)
        {
            RunningProcess->InterruptedBySignal = false;
            Manager->RemoveWaitingProcess(Device, RunningProcess->Id);
            return LINUX_ERR_EINTR;
        }
    }

    Manager->RemoveWaitingProcess(Device, RunningProcess->Id);

    uint64_t MaxEventsToRead = Count / sizeof(LinuxInputEvent);
    uint64_t EventsRead      = 0;

    LinuxInputEvent* DestinationEvents = reinterpret_cast<LinuxInputEvent*>(Buffer);
    while (EventsRead < MaxEventsToRead && Device->PendingEventCount > 0)
    {
        DestinationEvents[EventsRead] = Device->PendingEvents[Device->PendingEventHead];
        Device->PendingEventHead      = (Device->PendingEventHead + 1) % MAX_PENDING_EVENTS;
        --Device->PendingEventCount;
        ++EventsRead;
    }

    uint64_t BytesRead = EventsRead * sizeof(LinuxInputEvent);
    OpenFile->CurrentOffset += BytesRead;

    if (IsMouseEventDevice(Device) && BytesRead > 0)
    {
        static uint32_t MouseReadLogCount = 0;
        ++MouseReadLogCount;

        if ((MouseReadLogCount % EVENT_DEVICE_DEBUG_LOG_INTERVAL) == 1)
        {
            TTY* Terminal = GetEventDeviceLogTTY();
            if (Terminal != nullptr)
            {
                Terminal->printf_("event_dbg: mouse_read bytes=%lu events=%lu remaining=%u\n", static_cast<unsigned long>(BytesRead), static_cast<unsigned long>(EventsRead),
                                  static_cast<unsigned int>(Device->PendingEventCount));
            }
        }
    }

    return static_cast<int64_t>(BytesRead);
}

int64_t EventDevice::WriteFileOperation(File* OpenFile, const void* Buffer, uint64_t Count)
{
    (void) OpenFile;
    (void) Buffer;
    (void) Count;
    return LINUX_ERR_ENOSYS;
}

int64_t EventDevice::SeekFileOperation(File* OpenFile, int64_t Offset, int32_t Whence)
{
    (void) OpenFile;
    (void) Offset;
    (void) Whence;
    return LINUX_ERR_ENOSYS;
}

int64_t EventDevice::MemoryMapFileOperation(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address)
{
    (void) OpenFile;
    (void) Length;
    (void) Offset;
    (void) AddressSpace;
    (void) Address;
    return LINUX_ERR_ENOSYS;
}

int64_t EventDevice::PollFileOperation(File* OpenFile, uint32_t RequestedEvents, uint32_t* ReturnedEvents, LogicLayer* Logic, Process* RunningProcess)
{
    if (OpenFile == nullptr || OpenFile->Node == nullptr || ReturnedEvents == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    EventDevice* Device = reinterpret_cast<EventDevice*>(OpenFile->Node->NodeData);
    if (Device == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    *ReturnedEvents = 0;

    if ((RequestedEvents & static_cast<uint32_t>(LINUX_POLLOUT)) != 0)
    {
        *ReturnedEvents |= static_cast<uint32_t>(LINUX_POLLOUT);
    }

    if ((RequestedEvents & static_cast<uint32_t>(LINUX_POLLIN)) != 0)
    {
        if (Device->PendingEventCount > 0)
        {
            *ReturnedEvents |= static_cast<uint32_t>(LINUX_POLLIN);
        }
        else if (RunningProcess != nullptr)
        {
            EventDeviceManager* Manager = GetGlobalEventDeviceManager();
            if (Manager != nullptr)
            {
                Manager->AddWaitingProcess(Device, RunningProcess->Id);
            }
        }
    }

    if (Logic == nullptr || RunningProcess == nullptr)
    {
        return 0;
    }

    return 0;
}

int64_t EventDevice::IoctlFileOperation(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic, Process* RunningProcess)
{
    (void) RunningProcess;

    if (OpenFile == nullptr || OpenFile->Node == nullptr || Logic == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    // Normalize sign-extended 32-bit ioctl request values from userspace.
    uint32_t Request32 = static_cast<uint32_t>(Request);

    EventDevice* Device = reinterpret_cast<EventDevice*>(OpenFile->Node->NodeData);
    if (Device == nullptr)
    {
        return LINUX_ERR_EINVAL;
    }

    if (Request32 == LINUX_IOCTL_EVIOCGVERSION)
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        uint32_t Version = LINUX_EV_VERSION;
        return Logic->CopyFromKernelToUser(&Version, reinterpret_cast<void*>(Argument), sizeof(Version)) ? 0 : LINUX_ERR_EFAULT;
    }

    if (Request32 == LINUX_IOCTL_EVIOCGID)
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        LinuxInputId InputId = {LINUX_BUS_VIRTUAL, 0x1, 0x1, 0x1};
        return Logic->CopyFromKernelToUser(&InputId, reinterpret_cast<void*>(Argument), sizeof(InputId)) ? 0 : LINUX_ERR_EFAULT;
    }

    if (Request32 == LINUX_IOCTL_EVIOCGRAB)
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        int32_t GrabValue = 0;
        return Logic->CopyFromUserToKernel(reinterpret_cast<const void*>(Argument), &GrabValue, sizeof(GrabValue)) ? 0 : LINUX_ERR_EFAULT;
    }

    if (Request32 == LINUX_IOCTL_FIONREAD)
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        int32_t BytesAvailable = static_cast<int32_t>(Device->PendingEventCount * sizeof(LinuxInputEvent));
        return Logic->CopyFromKernelToUser(&BytesAvailable, reinterpret_cast<void*>(Argument), sizeof(BytesAvailable)) ? 0 : LINUX_ERR_EFAULT;
    }

    if (IsEventIoctlReadRequest(Request32, 0x06) || IsEventIoctlReadRequest(Request32, 0x07) || IsEventIoctlReadRequest(Request32, 0x08))
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        uint32_t NameBufferSize = IoctlSize(Request32);
        if (NameBufferSize == 0)
        {
            return LINUX_ERR_EINVAL;
        }

        char NameBuffer[EventDevice::MAX_EVENT_DEVICE_PATH] = {};
        const char* DefaultName = "";

        if (IsEventIoctlReadRequest(Request32, 0x06))
        {
            DefaultName = ResolveEventDeviceDisplayName(Device);
        }
        else if (IsEventIoctlReadRequest(Request32, 0x07))
        {
            if (Device->Kind == EVENT_DEVICE_KIND_MOUSE)
            {
                DefaultName = "isa0060/serio1/input0";
            }
            else if (Device->Kind == EVENT_DEVICE_KIND_KEYBOARD)
            {
                DefaultName = "isa0060/serio0/input0";
            }
            else
            {
                DefaultName = "virtual/input0";
            }
        }

        CopyCStringBounded(NameBuffer, sizeof(NameBuffer), DefaultName);

        uint32_t NameLength = static_cast<uint32_t>(strlen(NameBuffer));
        if (NameLength >= NameBufferSize)
        {
            NameBuffer[NameBufferSize - 1] = '\0';
            NameLength                     = NameBufferSize - 1;
        }

        return Logic->CopyFromKernelToUser(NameBuffer, reinterpret_cast<void*>(Argument), NameLength + 1) ? 0 : LINUX_ERR_EFAULT;
    }

    if (IsEventIoctlReadRequest(Request32, 0x09))
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        uint32_t PropSize = IoctlSize(Request32);
        if (PropSize == 0)
        {
            return LINUX_ERR_EINVAL;
        }

        if (PropSize > 8)
        {
            PropSize = 8;
        }

        uint8_t PropBitmap[8] = {};
        return Logic->CopyFromKernelToUser(PropBitmap, reinterpret_cast<void*>(Argument), PropSize) ? 0 : LINUX_ERR_EFAULT;
    }

    if (IsEventIoctlReadRequest(Request32, 0x18) || IsEventIoctlReadRequest(Request32, 0x19) || IsEventIoctlReadRequest(Request32, 0x1a) || IsEventIoctlReadRequest(Request32, 0x1b))
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        uint32_t StateSize = IoctlSize(Request32);
        if (StateSize == 0)
        {
            return LINUX_ERR_EINVAL;
        }

        if (StateSize > 128)
        {
            StateSize = 128;
        }

        uint8_t StateBitmap[128] = {};
        return Logic->CopyFromKernelToUser(StateBitmap, reinterpret_cast<void*>(Argument), StateSize) ? 0 : LINUX_ERR_EFAULT;
    }

    if (IoctlType(Request32) == static_cast<uint32_t>('E') && IoctlNumber(Request32) >= 0x20 && IoctlNumber(Request32) < 0x40 && (IoctlDirection(Request32) & LINUX_IOC_READ) != 0)
    {
        if (Argument == 0)
        {
            return LINUX_ERR_EFAULT;
        }

        uint32_t BitmapSize = IoctlSize(Request32);
        if (BitmapSize == 0)
        {
            return LINUX_ERR_EINVAL;
        }

        if (BitmapSize > 64)
        {
            BitmapSize = 64;
        }

        uint8_t Bitmap[64] = {};
        if (IoctlNumber(Request32) == 0x20)
        {
            SetBit(Bitmap, LINUX_EV_SYN);

            if (Device->Kind == EVENT_DEVICE_KIND_KEYBOARD)
            {
                SetBit(Bitmap, LINUX_EV_KEY);
                SetBit(Bitmap, LINUX_EV_MSC);
            }
            else if (Device->Kind == EVENT_DEVICE_KIND_MOUSE)
            {
                SetBit(Bitmap, LINUX_EV_KEY);
                SetBit(Bitmap, LINUX_EV_REL);
            }
            else
            {
                SetBit(Bitmap, LINUX_EV_KEY);
            }
        }
        else if (IoctlNumber(Request32) == 0x20 + LINUX_EV_KEY)
        {
            if (Device->Kind == EVENT_DEVICE_KIND_MOUSE)
            {
                SetBit(Bitmap, LINUX_BTN_LEFT);
                SetBit(Bitmap, LINUX_BTN_RIGHT);
                SetBit(Bitmap, LINUX_BTN_MIDDLE);
            }
            else
            {
                for (uint32_t KeyCode = 1; KeyCode <= 58; ++KeyCode)
                {
                    SetBit(Bitmap, KeyCode);
                }
            }
        }
        else if (IoctlNumber(Request32) == 0x20 + LINUX_EV_REL)
        {
            if (Device->Kind == EVENT_DEVICE_KIND_MOUSE)
            {
                SetBit(Bitmap, LINUX_REL_X);
                SetBit(Bitmap, LINUX_REL_Y);
            }
        }
        else if (IoctlNumber(Request32) == 0x20 + LINUX_EV_MSC)
        {
            if (Device->Kind == EVENT_DEVICE_KIND_KEYBOARD)
            {
                SetBit(Bitmap, LINUX_MSC_SCAN);
            }
        }

        return Logic->CopyFromKernelToUser(Bitmap, reinterpret_cast<void*>(Argument), BitmapSize) ? 0 : LINUX_ERR_EFAULT;
    }

    if (IsMouseEventDevice(Device))
    {
        static uint32_t UnsupportedMouseIoctlLogCount = 0;
        ++UnsupportedMouseIoctlLogCount;

        if ((UnsupportedMouseIoctlLogCount % EVENT_DEVICE_DEBUG_LOG_INTERVAL) == 1)
        {
            TTY* Terminal = GetEventDeviceLogTTY();
            if (Terminal != nullptr)
            {
                Terminal->printf_("event_dbg: mouse_ioctl_unsupported req=0x%llx size=%u nr=%u dir=%u\n", static_cast<unsigned long long>(Request),
                                  static_cast<unsigned int>(IoctlSize(Request32)), static_cast<unsigned int>(IoctlNumber(Request32)), static_cast<unsigned int>(IoctlDirection(Request32)));
            }
        }
    }

    return LINUX_ERR_ENOTTY;
}
