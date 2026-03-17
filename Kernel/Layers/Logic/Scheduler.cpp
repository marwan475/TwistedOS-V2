/**
 * File: Scheduler.cpp
 * Author: Marwan Mostafa
 * Description: Task scheduling algorithm implementation.
 */

#include "Scheduler.hpp"

#include <new>

/**
 * Function: Scheduler::Scheduler
 * Description: Constructs scheduler with no current process selected.
 * Parameters:
 *   None
 * Returns:
 *   Scheduler - Constructed scheduler instance.
 */
Scheduler::Scheduler() : CurrentProcess(nullptr)
{
}

/**
 * Function: Scheduler::~Scheduler
 * Description: Destroys scheduler and clears ready queue nodes.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
Scheduler::~Scheduler()
{
    ReadyQueue.ClearAndDelete();
    CurrentProcess = nullptr;
}

/**
 * Function: Scheduler::AddToReadyQueue
 * Description: Adds a process identifier to the ready queue.
 * Parameters:
 *   uint8_t ProcessId - Process ID to enqueue.
 * Returns:
 *   void - No return value.
 */
void Scheduler::AddToReadyQueue(uint8_t ProcessId)
{
    ProcessTag* NewTag = new ProcessTag;
    if (NewTag == nullptr)
    {
        return;
    }

    NewTag->Id          = ProcessId;
    NewTag->NextProcess = nullptr;

    ReadyQueue.PushBack(NewTag);
}

/**
 * Function: Scheduler::RemoveFromReadyQueue
 * Description: Removes the first matching process ID from the ready queue.
 * Parameters:
 *   uint8_t ProcessId - Process ID to remove.
 * Returns:
 *   bool - True if a node was removed, false if not found.
 */
bool Scheduler::RemoveFromReadyQueue(uint8_t ProcessId)
{
    ProcessTag* Node = ReadyQueue.FindFirst(&ProcessTag::Id, ProcessId);

    if (Node == nullptr)
    {
        return false;
    }

    ProcessTag* Next = ReadyQueue.Next(Node);
    ReadyQueue.Remove(Node);

    if (CurrentProcess == Node)
    {
        CurrentProcess = (Next != nullptr) ? nullptr : ReadyQueue.Head();
    }

    delete Node;

    if (ReadyQueue.IsEmpty())
    {
        CurrentProcess = nullptr;
    }

    return true;
}

/**
 * Function: Scheduler::SelectNextProcess
 * Description: Selects the next process to run using round-robin queue rotation.
 * Parameters:
 *   None
 * Returns:
 *   uint8_t - Selected process ID or 0xFF when queue is empty.
 */
uint8_t Scheduler::SelectNextProcess()
{
    if (ReadyQueue.IsEmpty())
    {
        return PROCESS_ID_INVALID;
    }

    if (CurrentProcess == nullptr)
    {
        CurrentProcess = ReadyQueue.Head();
        return CurrentProcess->Id;
    }

    if (ReadyQueue.Head() == CurrentProcess && ReadyQueue.Next(CurrentProcess) == nullptr)
    {
        return CurrentProcess->Id;
    }

    ReadyQueue.RotateFrontToBack();
    CurrentProcess = ReadyQueue.Head();
    return CurrentProcess->Id;
}
