/**
 * File: SynchronizationManager.cpp
 * Author: Marwan Mostafa
 * Description: Kernel synchronization primitive implementation.
 */

#include "SynchronizationManager.hpp"

#include <new>

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

    return 0xFF;
}