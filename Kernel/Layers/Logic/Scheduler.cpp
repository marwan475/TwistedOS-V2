#include "Scheduler.hpp"

#include <new>

Scheduler::Scheduler() : CurrentProcess(nullptr)
{
}

Scheduler::~Scheduler()
{
    ReadyQueue.ClearAndDelete();
    CurrentProcess = nullptr;
}

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

uint8_t Scheduler::SelectNextProcess()
{
    if (ReadyQueue.IsEmpty())
    {
        return 0xFF;
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
