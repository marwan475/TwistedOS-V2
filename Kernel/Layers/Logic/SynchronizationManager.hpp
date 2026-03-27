/**
 * File: SynchronizationManager.hpp
 * Author: Marwan Mostafa
 * Description: Kernel synchronization primitive declarations.
 */

#pragma once

#include "IntrusiveQueue.hpp"
#include "VirtualFileSystem.hpp"

#include <stdint.h>

struct SleepTag
{
    uint8_t   Id;
    uint64_t  WaitTicksRemaining;
    SleepTag* Next;
};

struct WaitForTTYInputTag
{
    uint8_t             Id;
    WaitForTTYInputTag* Next;
};

struct EventQueueTag
{
    uint8_t        ProcessId;
    uint64_t       FileDescriptor;
    uint64_t       Flags;
};

struct EpollWatchTag
{
    uint64_t       FileDescriptor;
    uint32_t       Events;
    uint64_t       UserData;
    EpollWatchTag* Next;
};

struct EventQueueEventTag
{
    uint64_t           FileDescriptor;
    uint32_t           Events;
    EventQueueEventTag* Next;
};

struct EventQueueKernelObject
{
    EventQueueTag                                              Queue;
    IntrusiveQueue<EpollWatchTag, &EpollWatchTag::Next>       Watches;
    IntrusiveQueue<EventQueueEventTag, &EventQueueEventTag::Next> Events;
    INode*                                                     Node;
    EventQueueKernelObject*                                    Next;
};

class SynchronizationManager
{
private:
    IntrusiveQueue<SleepTag, &SleepTag::Next>                     SleepQueue;
    IntrusiveQueue<WaitForTTYInputTag, &WaitForTTYInputTag::Next> TTYInputWaitQueue;
    IntrusiveQueue<EventQueueKernelObject, &EventQueueKernelObject::Next> EventQueueStore;

    EventQueueKernelObject* FindEventQueue(uint8_t ProcessId, uint64_t FileDescriptor) const;
    EpollWatchTag*          FindWatch(EventQueueKernelObject* Queue, uint64_t WatchedFileDescriptor) const;
    void                    ClearEventQueue(EventQueueKernelObject* Queue);

public:
    SynchronizationManager();
    ~SynchronizationManager();

    void    AddToSleepQueue(uint8_t Id, uint64_t WaitTicks);
    void    RemoveFromSleepQueue(uint8_t Id);
    void    AddToTTYInputWaitQueue(uint8_t Id);
    void    RemoveFromTTYInputWaitQueue(uint8_t Id);
    bool    CreateEventQueue(uint8_t ProcessId, uint64_t FileDescriptor, uint64_t Flags);
    bool    RemoveEventQueue(uint8_t ProcessId, uint64_t FileDescriptor);
    void    RemoveWatchesForFile(uint8_t ProcessId, uint64_t TargetFileDescriptor);
    bool    HasEventQueue(uint8_t ProcessId, uint64_t FileDescriptor) const;
    EventQueueKernelObject* GetEventQueue(uint8_t ProcessId, uint64_t FileDescriptor);
    int64_t ControlEventQueue(uint8_t ProcessId, uint64_t EpollFileDescriptor, int32_t Operation, uint64_t TargetFileDescriptor, uint32_t Events, uint64_t UserData);
    bool    DuplicateEventQueuesForProcess(uint8_t SourceProcessId, uint8_t DestProcessId);
    void    Tick();
    uint8_t GetNextProcessToWake();
    uint8_t GetNextTTYInputWaiter();
};