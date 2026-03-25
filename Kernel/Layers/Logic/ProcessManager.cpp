/**
 * File: ProcessManager.cpp
 * Author: Marwan Mostafa
 * Description: Process creation and lifecycle management implementation.
 */

#include "ProcessManager.hpp"

namespace
{
alignas(16) static uint8_t ProcessKernelSystemCallStacks[MAX_PROCESSES][PROCESS_KERNEL_SYSCALL_STACK_SIZE];

uint64_t ComputeKernelSystemCallStackTop(void* StackBase)
{
    if (StackBase == nullptr)
    {
        return 0;
    }

    uint64_t StackTop = reinterpret_cast<uint64_t>(StackBase) + PROCESS_KERNEL_SYSCALL_STACK_SIZE;
    StackTop          = (StackTop & ~0xFULL) - 8;
    return StackTop;
}

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

void ResetProcessMemoryMappings(Process& ProcessEntry)
{
    for (size_t MappingIndex = 0; MappingIndex < MAX_MEMORY_MAPPINGS_PER_PROCESS; ++MappingIndex)
    {
        ProcessEntry.MemoryMappings[MappingIndex] = {};
    }
}

void ResetProcessSignalActions(Process& ProcessEntry)
{
    if (ProcessEntry.SignalActions == nullptr)
    {
        return;
    }

    for (size_t SignalIndex = 0; SignalIndex < MAX_POSIX_SIGNALS_PER_PROCESS; ++SignalIndex)
    {
        ProcessEntry.SignalActions[SignalIndex] = {};
    }
}

void FreeProcessSignalActions(Process& ProcessEntry)
{
    delete[] ProcessEntry.SignalActions;
    ProcessEntry.SignalActions = nullptr;
}

bool EnsureProcessSignalActions(Process& ProcessEntry)
{
    if (ProcessEntry.SignalActions != nullptr)
    {
        return true;
    }

    ProcessEntry.SignalActions = new ProcessSignalAction[MAX_POSIX_SIGNALS_PER_PROCESS];

    ResetProcessSignalActions(ProcessEntry);
    return true;
}

bool InitializeProcessEntry(Process& ProcessEntry, ProcessState Status, ProcessLevel Level, FILE_TYPE FileType, void* StackPointer, void* KernelSystemCallStackBase, uint64_t ProgramBreak,
                            VirtualAddressSpace* AddressSpace, const CpuState& State, bool ResetFileTable)
{
    if (Status != PROCESS_TERMINATED)
    {
        if (!EnsureProcessSignalActions(ProcessEntry))
        {
            return false;
        }
    }
    else
    {
        FreeProcessSignalActions(ProcessEntry);
    }

    ProcessEntry.ParrentId                  = PROCESS_ID_INVALID;
    ProcessEntry.WaitingForVforkChild       = false;
    ProcessEntry.VforkChildId               = PROCESS_ID_INVALID;
    ProcessEntry.IsVforkChild               = false;
    ProcessEntry.VforkParentId              = PROCESS_ID_INVALID;
    ProcessEntry.WaitingForChild            = false;
    ProcessEntry.WaitingForSystemCallReturn = false;
    ProcessEntry.HasSavedSystemCallFrame    = false;
    ProcessEntry.HasPendingChildExit        = false;
    ProcessEntry.PendingChildId             = PROCESS_ID_INVALID;
    ProcessEntry.PendingChildStatus         = 0;
    ProcessEntry.SavedSystemCallFrame       = {};
    ProcessEntry.Status                     = Status;
    ProcessEntry.Level                      = Level;
    ProcessEntry.FileType                   = FileType;
    ProcessEntry.StackPointer               = StackPointer;
    ProcessEntry.KernelSystemCallStackBase  = KernelSystemCallStackBase;
    ProcessEntry.KernelSystemCallStackTop   = ComputeKernelSystemCallStackTop(ProcessEntry.KernelSystemCallStackBase);
    ProcessEntry.UserFSBase                 = 0;
    if (Status == PROCESS_TERMINATED)
    {
        ProcessEntry.ProcessGroupId = 0;
        ProcessEntry.SessionId      = 0;
    }
    else
    {
        ProcessEntry.ProcessGroupId = static_cast<int32_t>(ProcessEntry.Id);
        ProcessEntry.SessionId      = static_cast<int32_t>(ProcessEntry.Id);
    }
    ProcessEntry.BlockedSignalMask         = 0;
    ProcessEntry.PendingSignalMask         = 0;
    ProcessEntry.DebugSyscallTraceRemaining = 0;
    ProcessEntry.DebugIsXorgProcess        = false;
    ProcessEntry.ClearChildTidAddress      = nullptr;
    ProcessEntry.ProgramBreak              = ProgramBreak;
    ProcessEntry.RealIntervalTimerRemainingTicks = 0;
    ProcessEntry.RealIntervalTimerIntervalTicks  = 0;
    ProcessEntry.FileCreationMask          = 0022;
    ProcessEntry.ResourceLimitsInitialized = false;
    for (size_t ResourceLimitIndex = 0; ResourceLimitIndex < MAX_PROCESS_RESOURCE_LIMITS; ++ResourceLimitIndex)
    {
        ProcessEntry.ResourceLimitCurrent[ResourceLimitIndex] = 0;
        ProcessEntry.ResourceLimitMaximum[ResourceLimitIndex] = 0;
    }
    ProcessEntry.AddressSpace              = AddressSpace;
    ProcessEntry.CurrentFileSystemLocation = nullptr;
    ProcessEntry.RunningExecutableDentry   = nullptr;
    ProcessEntry.RunningExecutableINode    = nullptr;
    ProcessEntry.State                     = State;

    if (ResetFileTable)
    {
        ResetProcessFileTable(ProcessEntry);
    }

    ResetProcessSignalActions(ProcessEntry);
    ResetProcessMemoryMappings(ProcessEntry);
    return true;
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
    const CpuState EmptyState = {};

    for (size_t index = 0; index < MaxProcesses; ++index)
    {
        Processes[index].Id = static_cast<uint8_t>(index);
        InitializeProcessEntry(Processes[index], PROCESS_TERMINATED, PROCESS_LEVEL_KERNEL, FILE_TYPE_RAW_BINARY, nullptr, nullptr, 0, nullptr, EmptyState, true);
    }

    CurrentProcessId = 0;
}

/**
 * Function: ProcessManager::GetCurrentProcess
 * Description: Returns the currently tracked process regardless of ready/running/blocked state.
 * Parameters:
 *   None
 * Returns:
 *   Process* - Pointer to tracked current process, or nullptr if invalid/terminated.
 */
Process* ProcessManager::GetCurrentProcess()
{
    Process* CurrentProcess = GetProcessById(CurrentProcessId);
    if (CurrentProcess == nullptr || CurrentProcess->Status == PROCESS_TERMINATED)
    {
        return nullptr;
    }

    return CurrentProcess;
}

/**
 * Function: ProcessManager::GetRunningProcess
 * Description: Returns the currently running process.
 * Parameters:
 *   None
 * Returns:
 *   Process* - Pointer to running process, or nullptr if none exists.
 */
Process* ProcessManager::GetRunningProcess()
{
    Process* CurrentProcess = GetCurrentProcess();
    if (CurrentProcess != nullptr && CurrentProcess->Status == PROCESS_RUNNING)
    {
        return CurrentProcess;
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
            if (!InitializeProcessEntry(Processes[index], PROCESS_READY, PROCESS_LEVEL_KERNEL, FILE_TYPE_RAW_BINARY, StackPointer, &ProcessKernelSystemCallStacks[index][0], 0, nullptr, InitialState,
                                        true))
            {
                return PROCESS_ID_INVALID;
            }

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
            uint64_t ProgramBreak = (AddressSpace != nullptr) ? AddressSpace->GetHeapVirtualAddressStart() : 0;
            if (!InitializeProcessEntry(Processes[index], PROCESS_READY, PROCESS_LEVEL_USER, FileType, StackPointer, &ProcessKernelSystemCallStacks[index][0], ProgramBreak, AddressSpace, InitialState,
                                        true))
            {
                return PROCESS_ID_INVALID;
            }

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
    void*          StackPointer = Processes[Id].StackPointer;
    const CpuState EmptyState   = {};

    ReleaseProcessFileTable(Processes[Id]);
    InitializeProcessEntry(Processes[Id], PROCESS_TERMINATED, PROCESS_LEVEL_KERNEL, FILE_TYPE_RAW_BINARY, nullptr, nullptr, 0, nullptr, EmptyState, true);
    return StackPointer;
}
