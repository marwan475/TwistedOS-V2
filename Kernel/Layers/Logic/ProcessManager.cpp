/**
 * File: ProcessManager.cpp
 * Author: Marwan Mostafa
 * Description: Process creation and lifecycle management implementation.
 */

#include "ProcessManager.hpp"

namespace
{
void ResetProcessFileTable(Process& ProcessEntry)
{
    for (size_t FileIndex = 0; FileIndex < MAX_OPEN_FILES_PER_PROCESS; ++FileIndex)
    {
        ProcessEntry.FileTable[FileIndex] = nullptr;
    }
}

void ReleaseProcessFileTable(Process& ProcessEntry)
{
    for (size_t FileIndex = 0; FileIndex < MAX_OPEN_FILES_PER_PROCESS; ++FileIndex)
    {
        delete ProcessEntry.FileTable[FileIndex];
        ProcessEntry.FileTable[FileIndex] = nullptr;
    }
}
} // namespace

/**
 * Function: ProcessManager::ProcessManager
 * Description: Initializes process table entries and sets default current process ID.
 * Parameters:
 *   None
 * Returns:
 *   ProcessManager - Constructed process manager instance.
 */
ProcessManager::ProcessManager() : CurrentProcessId(PROCESS_ID_INVALID)
{
    for (size_t index = 0; index < MaxProcesses; ++index)
    {
        Processes[index].Id           = static_cast<uint8_t>(index);
        Processes[index].ParrentId    = PROCESS_ID_INVALID;
        Processes[index].WaitingForChild = false;
        Processes[index].Status       = PROCESS_TERMINATED;
        Processes[index].Level        = PROCESS_LEVEL_KERNEL;
        Processes[index].FileType     = FILE_TYPE_RAW_BINARY;
        Processes[index].StackPointer = nullptr;
        Processes[index].AddressSpace = nullptr;
        Processes[index].CurrentFileSystemLocation = nullptr;
        Processes[index].State        = {};
        ResetProcessFileTable(Processes[index]);
    }

    CurrentProcessId = 0;
}

/**
 * Function: ProcessManager::GetRunningProcess
 * Description: Returns the currently running process or the blocked current process during handoff.
 * Parameters:
 *   None
 * Returns:
 *   Process* - Pointer to running process, or nullptr if none exists.
 */
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

    CurrentProcessId = PROCESS_ID_INVALID;
    return nullptr;
}

/**
 * Function: ProcessManager::UpdateCurrentProcessId
 * Description: Updates tracked current process ID.
 * Parameters:
 *   uint8_t Id - Process ID to store as current.
 * Returns:
 *   void - No return value.
 */
void ProcessManager::UpdateCurrentProcessId(uint8_t Id)
{
    CurrentProcessId = Id;
}

/**
 * Function: ProcessManager::GetMaxProcesses
 * Description: Returns maximum process capacity.
 * Parameters:
 *   None
 * Returns:
 *   size_t - Maximum number of process slots.
 */
size_t ProcessManager::GetMaxProcesses() const
{
    return MaxProcesses;
}

/**
 * Function: ProcessManager::GetProcessById
 * Description: Returns process record by ID if within valid range.
 * Parameters:
 *   uint8_t Id - Process ID to look up.
 * Returns:
 *   Process* - Pointer to process entry or nullptr if ID is invalid.
 */
Process* ProcessManager::GetProcessById(uint8_t Id)
{
    if (Id >= MaxProcesses)
    {
        return nullptr;
    }
    return &Processes[Id];
}

/**
 * Function: ProcessManager::CreateKernelProcess
 * Description: Creates a kernel-level process entry in the first free slot.
 * Parameters:
 *   void* StackPointer - Base pointer associated with process stack allocation.
 *   CpuState InitialState - Initial CPU context for process startup.
 * Returns:
 *   uint8_t - Created process ID or 0xFF on failure.
 */
uint8_t ProcessManager::CreateKernelProcess(void* StackPointer, CpuState InitialState)
{
    for (size_t index = 0; index < MaxProcesses; ++index)
    {
        if (Processes[index].Status == PROCESS_TERMINATED)
        {
            Processes[index].ParrentId    = PROCESS_ID_INVALID;
            Processes[index].WaitingForChild = false;
            Processes[index].Status       = PROCESS_READY;
            Processes[index].Level        = PROCESS_LEVEL_KERNEL;
            Processes[index].FileType     = FILE_TYPE_RAW_BINARY;
            Processes[index].StackPointer = StackPointer;
            Processes[index].AddressSpace = nullptr;
            Processes[index].CurrentFileSystemLocation = nullptr;
            Processes[index].State        = InitialState;
            ResetProcessFileTable(Processes[index]);
            return Processes[index].Id;
        }
    }
    return PROCESS_ID_INVALID; // Indicate failure to create process
}

/**
 * Function: ProcessManager::CreateUserProcess
 * Description: Creates a user-level process entry in the first free slot.
 * Parameters:
 *   void* StackPointer - Base pointer associated with process stack allocation.
 *   CpuState InitialState - Initial CPU context for process startup.
 *   VirtualAddressSpace* AddressSpace - User process address space descriptor.
 *   FILE_TYPE FileType - Executable type used by the process.
 * Returns:
 *   uint8_t - Created process ID or 0xFF on failure.
 */
uint8_t ProcessManager::CreateUserProcess(void* StackPointer, CpuState InitialState, VirtualAddressSpace* AddressSpace, FILE_TYPE FileType)
{
    for (size_t index = 0; index < MaxProcesses; ++index)
    {
        if (Processes[index].Status == PROCESS_TERMINATED)
        {
            Processes[index].ParrentId    = PROCESS_ID_INVALID;
            Processes[index].WaitingForChild = false;
            Processes[index].Status       = PROCESS_READY;
            Processes[index].Level        = PROCESS_LEVEL_USER;
            Processes[index].FileType     = FileType;
            Processes[index].StackPointer = StackPointer;
            Processes[index].AddressSpace = AddressSpace;
            Processes[index].CurrentFileSystemLocation = nullptr;
            Processes[index].State        = InitialState;
            ResetProcessFileTable(Processes[index]);
            return Processes[index].Id;
        }
    }
    return PROCESS_ID_INVALID; // Indicate failure to create process
}

// Kill process and return its stack pointer for cleanup
/**
 * Function: ProcessManager::KillProcess
 * Description: Marks a process as terminated and returns its stack pointer for caller cleanup.
 * Parameters:
 *   uint8_t Id - Process ID to terminate.
 * Returns:
 *   void* - Stack pointer associated with the process, or nullptr on invalid ID.
 */
void* ProcessManager::KillProcess(uint8_t Id)
{
    if (Id >= MaxProcesses)
    {
        return nullptr; // Invalid process ID
    }
    void* StackPointer = Processes[Id].StackPointer;

    ReleaseProcessFileTable(Processes[Id]);
    Processes[Id].ParrentId = PROCESS_ID_INVALID;
    Processes[Id].WaitingForChild = false;
    Processes[Id].Status    = PROCESS_TERMINATED;
    Processes[Id].FileType  = FILE_TYPE_RAW_BINARY;
    Processes[Id].StackPointer = nullptr;
    Processes[Id].AddressSpace = nullptr;
    Processes[Id].CurrentFileSystemLocation = nullptr;
    Processes[Id].State = {};
    return StackPointer;
}
