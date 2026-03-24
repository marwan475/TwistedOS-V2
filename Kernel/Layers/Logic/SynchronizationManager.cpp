/**
 * File: SynchronizationManager.cpp
 * Author: Marwan Mostafa
 * Description: Kernel synchronization primitive implementation.
 */

#include "SynchronizationManager.hpp"

#include "ProcessManager.hpp"

#include <new>

namespace
{
constexpr int64_t LINUX_ERR_EFAULT = -14;
constexpr int64_t LINUX_ERR_ENOSYS = -38;
constexpr int64_t LINUX_ERR_ENOMEM = -12;
constexpr int64_t LINUX_ERR_EINVAL = -22;
constexpr int64_t LINUX_ERR_ENOENT = -2;
constexpr int64_t LINUX_ERR_EEXIST = -17;
constexpr int16_t LINUX_POLLIN     = 0x0001;
constexpr int16_t LINUX_POLLOUT    = 0x0004;
constexpr uint8_t INVALID_PROCESS_ID = 0xFF;

constexpr int32_t LINUX_EPOLL_CTL_ADD = 1;
constexpr int32_t LINUX_EPOLL_CTL_DEL = 2;
constexpr int32_t LINUX_EPOLL_CTL_MOD = 3;

int64_t EventQueueReadFileOperation(File* OpenFile, void* Buffer, uint64_t Count)
{
    (void) OpenFile;
    (void) Buffer;
    (void) Count;
    return LINUX_ERR_ENOSYS;
}

int64_t EventQueueWriteFileOperation(File* OpenFile, const void* Buffer, uint64_t Count)
{
    (void) OpenFile;
    (void) Buffer;
    (void) Count;
    return LINUX_ERR_ENOSYS;
}

int64_t EventQueueSeekFileOperation(File* OpenFile, int64_t Offset, int32_t Whence)
{
    (void) OpenFile;
    (void) Offset;
    (void) Whence;
    return LINUX_ERR_ENOSYS;
}

int64_t EventQueueMemoryMapFileOperation(File* OpenFile, uint64_t Length, uint64_t Offset, VirtualAddressSpace* AddressSpace, uint64_t* Address)
{
    (void) OpenFile;
    (void) Length;
    (void) Offset;
    (void) AddressSpace;
    (void) Address;
    return LINUX_ERR_ENOSYS;
}

int64_t EventQueuePollFileOperation(File* OpenFile, uint32_t RequestedEvents, uint32_t* ReturnedEvents, LogicLayer* Logic, Process* RunningProcess)
{
    (void) Logic;
    (void) RunningProcess;

    if (OpenFile == nullptr || OpenFile->Node == nullptr || ReturnedEvents == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    EventQueueKernelObject* EventQueue = reinterpret_cast<EventQueueKernelObject*>(OpenFile->Node->NodeData);
    if (EventQueue == nullptr)
    {
        return LINUX_ERR_EFAULT;
    }

    uint32_t Revents = 0;
    if ((RequestedEvents & static_cast<uint32_t>(LINUX_POLLIN)) != 0 && !EventQueue->Events.IsEmpty())
    {
        Revents |= static_cast<uint32_t>(LINUX_POLLIN);
    }

    if ((RequestedEvents & static_cast<uint32_t>(LINUX_POLLOUT)) != 0)
    {
        Revents |= static_cast<uint32_t>(LINUX_POLLOUT);
    }

    *ReturnedEvents = Revents;
    return 0;
}

int64_t EventQueueIoctlFileOperation(File* OpenFile, uint64_t Request, uint64_t Argument, LogicLayer* Logic, Process* RunningProcess)
{
    (void) OpenFile;
    (void) Request;
    (void) Argument;
    (void) Logic;
    (void) RunningProcess;
    return LINUX_ERR_ENOSYS;
}

FileOperations EventQueueFileOperations = {
        &EventQueueReadFileOperation, &EventQueueWriteFileOperation, &EventQueueSeekFileOperation, &EventQueueMemoryMapFileOperation, &EventQueuePollFileOperation,
        &EventQueueIoctlFileOperation,
};
} // namespace

/**
 * Function: SynchronizationManager::SynchronizationManager
 * Description: Constructs synchronization manager.
 * Parameters:
 *   None
 * Returns:
 *   SynchronizationManager - Constructed synchronization manager instance.
 */
SynchronizationManager::SynchronizationManager()
{
}

/**
 * Function: SynchronizationManager::~SynchronizationManager
 * Description: Destroys synchronization manager and clears sleep queue entries.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
SynchronizationManager::~SynchronizationManager()
{
    SleepQueue.ClearAndDelete();
    TTYInputWaitQueue.ClearAndDelete();

    while (!EventQueueStore.IsEmpty())
    {
        EventQueueKernelObject* Queue = EventQueueStore.PopFront();
        ClearEventQueue(Queue);
        delete Queue;
    }
}

EventQueueKernelObject* SynchronizationManager::FindEventQueue(uint8_t ProcessId, uint64_t FileDescriptor) const
{
    EventQueueKernelObject* Queue = EventQueueStore.Head();
    while (Queue != nullptr)
    {
        if (Queue->Queue.ProcessId == ProcessId && Queue->Queue.FileDescriptor == FileDescriptor)
        {
            return Queue;
        }

        Queue = EventQueueStore.Next(Queue);
    }

    return nullptr;
}

EpollWatchTag* SynchronizationManager::FindWatch(EventQueueKernelObject* Queue, uint64_t WatchedFileDescriptor) const
{
    if (Queue == nullptr)
    {
        return nullptr;
    }

    EpollWatchTag* Watch = Queue->Watches.Head();
    while (Watch != nullptr)
    {
        if (Watch->FileDescriptor == WatchedFileDescriptor)
        {
            return Watch;
        }

        Watch = Queue->Watches.Next(Watch);
    }

    return nullptr;
}

void SynchronizationManager::ClearEventQueue(EventQueueKernelObject* Queue)
{
    if (Queue == nullptr)
    {
        return;
    }

    while (!Queue->Watches.IsEmpty())
    {
        EpollWatchTag* Watch = Queue->Watches.PopFront();
        delete Watch;
    }

    while (!Queue->Events.IsEmpty())
    {
        EventQueueEventTag* Event = Queue->Events.PopFront();
        delete Event;
    }

    if (Queue->Node != nullptr)
    {
        delete Queue->Node;
        Queue->Node = nullptr;
    }
}

/**
 * Function: SynchronizationManager::AddToSleepQueue
 * Description: Adds a process to the sleep queue with remaining wait ticks.
 * Parameters:
 *   uint8_t Id - Process ID to sleep.
 *   uint64_t WaitTicks - Number of ticks to wait.
 * Returns:
 *   void - No return value.
 */
void SynchronizationManager::AddToSleepQueue(uint8_t Id, uint64_t WaitTicks)
{
    SleepTag* NewTag = new SleepTag;
    if (NewTag == nullptr)
    {
        return;
    }

    NewTag->Id                 = Id;
    NewTag->WaitTicksRemaining = WaitTicks;
    NewTag->Next               = nullptr;

    SleepQueue.PushBack(NewTag);
}

/**
 * Function: SynchronizationManager::RemoveFromSleepQueue
 * Description: Removes a process from the sleep queue.
 * Parameters:
 *   uint8_t Id - Process ID to remove.
 * Returns:
 *   void - No return value.
 */
void SynchronizationManager::RemoveFromSleepQueue(uint8_t Id)
{
    SleepTag* Node = SleepQueue.FindFirst(&SleepTag::Id, Id);

    if (Node == nullptr)
    {
        return;
    }

    SleepQueue.Remove(Node);

    delete Node;
}

void SynchronizationManager::AddToTTYInputWaitQueue(uint8_t Id)
{
    if (TTYInputWaitQueue.FindFirst(&WaitForTTYInputTag::Id, Id) != nullptr)
    {
        return;
    }

    WaitForTTYInputTag* NewTag = new WaitForTTYInputTag;
    if (NewTag == nullptr)
    {
        return;
    }

    NewTag->Id   = Id;
    NewTag->Next = nullptr;

    TTYInputWaitQueue.PushBack(NewTag);
}

void SynchronizationManager::RemoveFromTTYInputWaitQueue(uint8_t Id)
{
    WaitForTTYInputTag* Node = TTYInputWaitQueue.FindFirst(&WaitForTTYInputTag::Id, Id);

    if (Node == nullptr)
    {
        return;
    }

    TTYInputWaitQueue.Remove(Node);

    delete Node;
}

bool SynchronizationManager::CreateEventQueue(uint8_t ProcessId, uint64_t FileDescriptor, uint64_t Flags)
{
    if (FindEventQueue(ProcessId, FileDescriptor) != nullptr)
    {
        return false;
    }

    EventQueueKernelObject* NewQueue = new EventQueueKernelObject;
    if (NewQueue == nullptr)
    {
        return false;
    }

    INode* Node = new INode;
    if (Node == nullptr)
    {
        delete NewQueue;
        return false;
    }

    *Node             = {};
    Node->NodeType    = INODE_FILE;
    Node->NodeData    = NewQueue;
    Node->FileOps     = &EventQueueFileOperations;
    NewQueue->Node    = Node;

    NewQueue->Queue.ProcessId      = ProcessId;
    NewQueue->Queue.FileDescriptor = FileDescriptor;
    NewQueue->Queue.Flags          = Flags;
    NewQueue->Next                 = nullptr;

    EventQueueStore.PushBack(NewQueue);
    return true;
}

bool SynchronizationManager::RemoveEventQueue(uint8_t ProcessId, uint64_t FileDescriptor)
{
    EventQueueKernelObject* Queue = FindEventQueue(ProcessId, FileDescriptor);
    if (Queue == nullptr)
    {
        return false;
    }

    EventQueueStore.Remove(Queue);
    ClearEventQueue(Queue);
    delete Queue;
    return true;
}

bool SynchronizationManager::HasEventQueue(uint8_t ProcessId, uint64_t FileDescriptor) const
{
    return FindEventQueue(ProcessId, FileDescriptor) != nullptr;
}

EventQueueKernelObject* SynchronizationManager::GetEventQueue(uint8_t ProcessId, uint64_t FileDescriptor)
{
    return FindEventQueue(ProcessId, FileDescriptor);
}

int64_t SynchronizationManager::ControlEventQueue(
        uint8_t ProcessId,
        uint64_t EpollFileDescriptor,
        int32_t Operation,
        uint64_t TargetFileDescriptor,
        uint32_t Events,
        uint64_t UserData)
{
    if (TargetFileDescriptor == EpollFileDescriptor)
    {
        return LINUX_ERR_EINVAL;
    }

    EventQueueKernelObject* Queue = FindEventQueue(ProcessId, EpollFileDescriptor);
    if (Queue == nullptr)
    {
        return LINUX_ERR_ENOENT;
    }

    EpollWatchTag* Watch = FindWatch(Queue, TargetFileDescriptor);

    switch (Operation)
    {
        case LINUX_EPOLL_CTL_ADD:
        {
            if (Watch != nullptr)
            {
                return LINUX_ERR_EEXIST;
            }

            EpollWatchTag* NewWatch = new EpollWatchTag;
            if (NewWatch == nullptr)
            {
                return LINUX_ERR_ENOMEM;
            }

            NewWatch->FileDescriptor = TargetFileDescriptor;
            NewWatch->Events         = Events;
            NewWatch->UserData       = UserData;
            NewWatch->Next           = nullptr;
            Queue->Watches.PushBack(NewWatch);
            return 0;
        }
        case LINUX_EPOLL_CTL_DEL:
        {
            if (Watch == nullptr)
            {
                return LINUX_ERR_ENOENT;
            }

            Queue->Watches.Remove(Watch);
            delete Watch;
            return 0;
        }
        case LINUX_EPOLL_CTL_MOD:
        {
            if (Watch == nullptr)
            {
                return LINUX_ERR_ENOENT;
            }

            Watch->Events   = Events;
            Watch->UserData = UserData;
            return 0;
        }
        default:
            return LINUX_ERR_EINVAL;
    }
}

/**
 * Function: SynchronizationManager::Tick
 * Description: Decrements remaining wait ticks for all sleeping processes.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void SynchronizationManager::Tick()
{
    SleepTag* Node = SleepQueue.Head();
    while (Node != nullptr)
    {
        if (Node->WaitTicksRemaining > 0)
        {
            --Node->WaitTicksRemaining;
        }

        Node = SleepQueue.Next(Node);
    }
}

/**
 * Function: SynchronizationManager::GetNextProcessToWake
 * Description: Returns ID of next process whose sleep has expired.
 * Parameters:
 *   None
 * Returns:
 *   uint8_t - Process ID ready to wake, or 0xFF if none.
 */
uint8_t SynchronizationManager::GetNextProcessToWake()
{
    SleepTag* Node = SleepQueue.FindFirst(&SleepTag::WaitTicksRemaining, static_cast<uint64_t>(0));
    if (Node != nullptr)
    {
        return Node->Id;
    }

    return INVALID_PROCESS_ID;
}

uint8_t SynchronizationManager::GetNextTTYInputWaiter()
{
    WaitForTTYInputTag* Node = TTYInputWaitQueue.Head();
    if (Node != nullptr)
    {
        return Node->Id;
    }

    return INVALID_PROCESS_ID;
}