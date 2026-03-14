#include "Scheduler.hpp"

#include <new>

Scheduler::Scheduler() : ReadyQueue(nullptr), CurrentProcess(nullptr)
{
}

Scheduler::~Scheduler()
{
    ProcessTag* Node = ReadyQueue;
    while (Node != nullptr)
    {
        ProcessTag* Next = Node->NextProcess;
        delete Node;
        Node = Next;
    }

    ReadyQueue     = nullptr;
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

    if (ReadyQueue == nullptr)
    {
        ReadyQueue = NewTag;
        return;
    }

    ProcessTag* Tail = ReadyQueue;
    while (Tail->NextProcess != nullptr)
    {
        Tail = Tail->NextProcess;
    }

    Tail->NextProcess = NewTag;
}

bool Scheduler::RemoveFromReadyQueue(uint8_t ProcessId)
{
    ProcessTag* Previous = nullptr;
    ProcessTag* Node     = ReadyQueue;

    while (Node != nullptr && Node->Id != ProcessId)
    {
        Previous = Node;
        Node     = Node->NextProcess;
    }

    if (Node == nullptr)
    {
        return false;
    }

    ProcessTag* Next = Node->NextProcess;

    if (Previous == nullptr)
    {
        ReadyQueue = Next;
    }
    else
    {
        Previous->NextProcess = Next;
    }

    if (CurrentProcess == Node)
    {
        CurrentProcess = (Next != nullptr) ? Next : ReadyQueue;
    }

    delete Node;

    if (ReadyQueue == nullptr)
    {
        CurrentProcess = nullptr;
    }

    return true;
}

uint8_t Scheduler::SelectNextProcess()
{
    if (ReadyQueue == nullptr)
    {
        return 0xFF;
    }

    if (CurrentProcess == nullptr)
    {
        CurrentProcess = ReadyQueue;
        return CurrentProcess->Id;
    }

    if (ReadyQueue == CurrentProcess && CurrentProcess->NextProcess == nullptr)
    {
        return CurrentProcess->Id;
    }

    ProcessTag* OldCurrent = CurrentProcess;
    ReadyQueue             = OldCurrent->NextProcess;

    OldCurrent->NextProcess = nullptr;
    ProcessTag* Tail        = ReadyQueue;
    while (Tail->NextProcess != nullptr)
    {
        Tail = Tail->NextProcess;
    }
    Tail->NextProcess = OldCurrent;

    CurrentProcess = ReadyQueue;
    return CurrentProcess->Id;
}
