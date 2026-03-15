#include "ProcessManager.hpp"

ProcessManager::ProcessManager() : CurrentProcessId(0xFF)
{
    for (size_t index = 0; index < MaxProcesses; ++index)
    {
        Processes[index].Id           = static_cast<uint8_t>(index);
        Processes[index].Status       = PROCESS_TERMINATED;
        Processes[index].StackPointer = nullptr;
        Processes[index].State        = {};
    }

    CurrentProcessId = 0;
}

Process* ProcessManager::GetRunningProcess()
{
    if (GetProcessById(CurrentProcessId)->Status == PROCESS_BLOCKED)
    {
        return &Processes[CurrentProcessId];
        // Means that the current process was just running but got blocked by LogicLayer::SleepProcess,
        // so we return it as the running process so that it can be scheduled out
    }

    for (size_t index = 0; index < MaxProcesses; ++index)
    {
        if (Processes[index].Status == PROCESS_RUNNING)
        {
            CurrentProcessId = Processes[index].Id;
            return &Processes[index];
        }
    }

    CurrentProcessId = 0xFF;
    return nullptr;
}

void ProcessManager::UpdateCurrentProcessId(uint8_t Id)
{
    CurrentProcessId = Id;
}

size_t ProcessManager::GetMaxProcesses() const
{
    return MaxProcesses;
}

Process* ProcessManager::GetProcessById(uint8_t Id)
{
    if (Id >= MaxProcesses)
    {
        return nullptr;
    }
    return &Processes[Id];
}

uint8_t ProcessManager::CreateProcess(void* StackPointer, CpuState InitialState)
{
    for (size_t index = 0; index < MaxProcesses; ++index)
    {
        if (Processes[index].Status == PROCESS_TERMINATED)
        {
            Processes[index].Status       = PROCESS_READY;
            Processes[index].StackPointer = StackPointer;
            Processes[index].State        = InitialState;
            return Processes[index].Id;
        }
    }
    return 0xFF; // Indicate failure to create process
}

// Kill process and return its stack pointer for cleanup
void* ProcessManager::KillProcess(uint8_t Id)
{
    if (Id >= MaxProcesses)
    {
        return nullptr; // Invalid process ID
    }
    Processes[Id].Status = PROCESS_TERMINATED;
    return Processes[Id].StackPointer;
}
