#include "SynchronizationManager.hpp"

#include <new>

SynchronizationManager::SynchronizationManager() : SleepQueue(nullptr)
{
}

SynchronizationManager::~SynchronizationManager()
{
	SleepTag* Node = SleepQueue;
	while (Node != nullptr)
	{
		SleepTag* Next = Node->Next;
		delete Node;
		Node = Next;
	}

	SleepQueue = nullptr;
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

	if (SleepQueue == nullptr)
	{
		SleepQueue = NewTag;
		return;
	}

	SleepTag* Tail = SleepQueue;
	while (Tail->Next != nullptr)
	{
		Tail = Tail->Next;
	}

	Tail->Next = NewTag;
}

void SynchronizationManager::RemoveFromSleepQueue(uint8_t Id)
{
	SleepTag* Previous = nullptr;
	SleepTag* Node     = SleepQueue;

	while (Node != nullptr && Node->Id != Id)
	{
		Previous = Node;
		Node     = Node->Next;
	}

	if (Node == nullptr)
	{
		return;
	}

	SleepTag* Next = Node->Next;

	if (Previous == nullptr)
	{
		SleepQueue = Next;
	}
	else
	{
		Previous->Next = Next;
	}

	delete Node;
}

void SynchronizationManager::Tick()
{
	SleepTag* Node = SleepQueue;
	while (Node != nullptr)
	{
		if (Node->WaitTicksRemaining > 0)
		{
			--Node->WaitTicksRemaining;
		}

		Node = Node->Next;
	}
}

uint8_t SynchronizationManager::GetNextProcessToWake()
{
	SleepTag* Node = SleepQueue;
	while (Node != nullptr)
	{
		if (Node->WaitTicksRemaining == 0)
		{
			return Node->Id;
		}

		Node = Node->Next;
	}

	return 0xFF;
}