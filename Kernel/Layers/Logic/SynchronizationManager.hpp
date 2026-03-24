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
    bool    HasEventQueue(uint8_t ProcessId, uint64_t FileDescriptor) const;
    EventQueueKernelObject* GetEventQueue(uint8_t ProcessId, uint64_t FileDescriptor);
    void    Tick();
    uint8_t GetNextProcessToWake();
    uint8_t GetNextTTYInputWaiter();
};