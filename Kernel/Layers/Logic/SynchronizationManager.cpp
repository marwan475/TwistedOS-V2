#include "SynchronizationManager.hpp"

#include <new>

SynchronizationManager::SynchronizationManager()
{
}

SynchronizationManager::~SynchronizationManager()
{
    SleepQueue.ClearAndDelete();
}

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

uint8_t SynchronizationManager::GetNextProcessToWake()
{
    SleepTag* Node = SleepQueue.FindFirst(&SleepTag::WaitTicksRemaining, static_cast<uint64_t>(0));
    if (Node != nullptr)
    {
        return Node->Id;
    }

    return 0xFF;
}