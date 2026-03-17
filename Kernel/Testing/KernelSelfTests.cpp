/**
 * File: KernelSelfTests.cpp
 * Author: Marwan Mostafa
 * Description: Kernel self-test implementations.
 */

#include "KernelSelfTests.hpp"

#include <CommonUtils.hpp>
#include <Layers/Dispatcher.hpp>
#include <Layers/Logic/ELFManager.hpp>

void KernelValidatorTask();

namespace
{
constexpr uint8_t INVALID_PROCESS_ID = 0xFF;

constexpr uint64_t KERNEL_SLEEP_FAST_TICKS   = 10;
constexpr uint64_t KERNEL_SLEEP_MEDIUM_TICKS = 300;
constexpr uint64_t KERNEL_SLEEP_SLOW_TICKS   = 800;
constexpr uint64_t KERNEL_BURST_SLEEP_TICKS  = 1;
constexpr uint64_t VALIDATOR_SLEEP_TICKS     = 10;

constexpr uint32_t USER_PROCESS_INSTANCE_COUNT = 1;
constexpr uint32_t TEST_MAX_PROCESS_COUNT      = 32;

constexpr uint64_t FAST_MIN_CYCLES   = 4;
constexpr uint64_t MEDIUM_MIN_CYCLES = 3;
constexpr uint64_t SLOW_MIN_CYCLES   = 2;
constexpr uint64_t BURST_MIN_LOOPS   = 500;

enum SelfTestPhase
{
    // Phase 1: run memory-focused checks before any heavy multitasking load.
    SELF_TEST_PHASE_MEMORY = 0,
    // Phase 2: spawn kernel/user processes required for scheduling validation.
    SELF_TEST_PHASE_MULTITASK_SETUP,
    // Phase 3: monitor runtime counters until pass criteria are satisfied.
    SELF_TEST_PHASE_MULTITASK_MONITOR,
    // Phase 4: test suite completed successfully.
    SELF_TEST_PHASE_COMPLETE,
    // Terminal fallback phase for failure or post-completion idle.
    SELF_TEST_PHASE_FAILED,
};

struct KernelSelfTestState
{
    Dispatcher* DispatcherRef;

    uint8_t TestProcessIds[TEST_MAX_PROCESS_COUNT];
    uint8_t TestProcessCount;

    uint8_t KernelSleepFastId;
    uint8_t KernelSleepMediumId;
    uint8_t KernelSleepSlowId;
    uint8_t KernelBurstId;
    uint8_t KernelValidatorId;

    uint64_t FastCycles;
    uint64_t MediumCycles;
    uint64_t SlowCycles;
    uint64_t BurstLoops;

    uint64_t SyscallOneCount;
    uint64_t SyscallTwoCount;
    uint64_t NextMultitaskProgressBurstLoops;

    bool MemoryVirtualOk;
    bool MemoryPhysicalOk;
    bool MemoryHeapOk;
    bool UserCreationResultLogged;
    bool MultitaskSleepResultLogged;

    uint32_t TestsPassed;
    uint32_t TestsFailed;
    bool     SummaryPrinted;

    SelfTestPhase Phase;
    bool          Initialized;
    bool          Passed;
};

KernelSelfTestState State = {
        nullptr,
        {},
        0,
        INVALID_PROCESS_ID,
        INVALID_PROCESS_ID,
        INVALID_PROCESS_ID,
        INVALID_PROCESS_ID,
        INVALID_PROCESS_ID,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        false,
        false,
        false,
        false,
        false,
        0,
        0,
        false,
        SELF_TEST_PHASE_MEMORY,
        false,
        false,
};

uint8_t CreateUserProcessFromInitramfs(Dispatcher* ActiveDispatcher, const char* Path, bool RequireElfImage);

/**
 * Function: ValidateImageAsElf
 * Description: Parses and validates an in-memory image as an ELF binary.
 * Parameters:
 *   Dispatcher* ActiveDispatcher - Dispatcher used to access the ELF manager.
 *   const void* Data - Pointer to the in-memory image data.
 *   uint64_t Size - Size of the image data in bytes.
 * Returns:
 *   bool - true if the image is a valid ELF file; otherwise false.
 */
bool ValidateImageAsElf(Dispatcher* ActiveDispatcher, const void* Data, uint64_t Size)
{
    if (ActiveDispatcher == nullptr || Data == nullptr || Size < sizeof(ELFHeader))
    {
        return false;
    }

    ELFManager* ElfManager = ActiveDispatcher->GetLogicLayer()->GetELFManger();
    if (ElfManager == nullptr)
    {
        return false;
    }

    ELFHeader Header = ElfManager->ParseELF(reinterpret_cast<uint64_t>(Data));
    return ElfManager->ValidateELF(Header);
}

/**
 * Function: RequireDispatcher
 * Description: Returns the active dispatcher and halts if no dispatcher is available.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   Dispatcher* - Active dispatcher pointer.
 */
Dispatcher* RequireDispatcher()
{
    Dispatcher* ActiveDispatcher = State.DispatcherRef;
    if (ActiveDispatcher == nullptr)
    {
        ActiveDispatcher = Dispatcher::GetActive();
    }

    if (ActiveDispatcher == nullptr)
    {
        while (1)
        {
            __asm__ __volatile__("hlt");
        }
    }

    return ActiveDispatcher;
}

/**
 * Function: RegisterTestProcess
 * Description: Registers a process ID in the self-test process list when valid and space is available.
 * Parameters:
 *   uint8_t ProcessId - Process identifier to track in self-test state.
 * Returns:
 *   void - No value is returned.
 */
void RegisterTestProcess(uint8_t ProcessId)
{
    if (ProcessId == INVALID_PROCESS_ID)
    {
        return;
    }

    if (State.TestProcessCount >= TEST_MAX_PROCESS_COUNT)
    {
        return;
    }

    State.TestProcessIds[State.TestProcessCount] = ProcessId;
    ++State.TestProcessCount;
}

/**
 * Function: ResetMultitaskCounters
 * Description: Resets multitasking counters and related logging flags for a new monitoring run.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
void ResetMultitaskCounters()
{
    State.FastCycles                      = 0;
    State.MediumCycles                    = 0;
    State.SlowCycles                      = 0;
    State.BurstLoops                      = 0;
    State.SyscallOneCount                 = 0;
    State.SyscallTwoCount                 = 0;
    State.NextMultitaskProgressBurstLoops = 200;
    State.UserCreationResultLogged        = false;
    State.MultitaskSleepResultLogged      = false;
}

/**
 * Function: KernelSleepFastTask
 * Description: Kernel test task that tracks fast-cycle activity and sleeps for a short interval.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
void KernelSleepFastTask()
{
    Dispatcher* ActiveDispatcher = RequireDispatcher();

    while (1)
    {
        ++State.FastCycles;

        ActiveDispatcher->GetLogicLayer()->SleepProcess(State.KernelSleepFastId, KERNEL_SLEEP_FAST_TICKS);
    }
}

/**
 * Function: KernelSleepMediumTask
 * Description: Kernel test task that tracks medium-cycle activity and sleeps for a medium interval.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
void KernelSleepMediumTask()
{
    Dispatcher* ActiveDispatcher = RequireDispatcher();

    while (1)
    {
        ++State.MediumCycles;

        ActiveDispatcher->GetLogicLayer()->SleepProcess(State.KernelSleepMediumId, KERNEL_SLEEP_MEDIUM_TICKS);
    }
}

/**
 * Function: KernelSleepSlowTask
 * Description: Kernel test task that tracks slow-cycle activity and sleeps for a longer interval.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
void KernelSleepSlowTask()
{
    Dispatcher* ActiveDispatcher = RequireDispatcher();

    while (1)
    {
        ++State.SlowCycles;

        ActiveDispatcher->GetLogicLayer()->SleepProcess(State.KernelSleepSlowId, KERNEL_SLEEP_SLOW_TICKS);
    }
}

/**
 * Function: KernelBurstTask
 * Description: Kernel test task that spins in bursts and periodically yields via short sleep.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
void KernelBurstTask()
{
    Dispatcher* ActiveDispatcher = RequireDispatcher();

    while (1)
    {
        ++State.BurstLoops;

        if ((State.BurstLoops % 150) == 0)
        {
            ActiveDispatcher->GetLogicLayer()->SleepProcess(State.KernelBurstId, KERNEL_BURST_SLEEP_TICKS);
        }

        __asm__ __volatile__("pause");
    }
}

/**
 * Function: KernelChecksPassed
 * Description: Evaluates whether kernel multitasking counters satisfy minimum pass thresholds.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   bool - true if all kernel-side thresholds are met; otherwise false.
 */
bool KernelChecksPassed()
{
    return State.FastCycles >= FAST_MIN_CYCLES && State.MediumCycles >= MEDIUM_MIN_CYCLES && State.SlowCycles >= SLOW_MIN_CYCLES && State.BurstLoops >= BURST_MIN_LOOPS;
}

/**
 * Function: UserChecksPassed
 * Description: Evaluates whether user-process syscall counters satisfy minimum pass thresholds.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   bool - true if user syscall targets are met; otherwise false.
 */
bool UserChecksPassed()
{
    return State.SyscallOneCount >= USER_PROCESS_INSTANCE_COUNT && State.SyscallTwoCount >= USER_PROCESS_INSTANCE_COUNT;
}

/**
 * Function: LogTestResult
 * Description: Records pass/fail totals and prints a formatted self-test result line.
 * Parameters:
 *   Dispatcher* ActiveDispatcher - Dispatcher used to access console output.
 *   const char* TestName - Name of the test being reported.
 *   bool Passed - Result flag indicating pass (true) or fail (false).
 * Returns:
 *   void - No value is returned.
 */
void LogTestResult(Dispatcher* ActiveDispatcher, const char* TestName, bool Passed)
{
    if (Passed)
    {
        ++State.TestsPassed;
    }
    else
    {
        ++State.TestsFailed;
    }

    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] [%s] test result=%s\n", TestName, Passed ? "PASS" : "FAIL");
}

/**
 * Function: PrintSummaryIfNeeded
 * Description: Prints final self-test summary once and suppresses duplicate summary output.
 * Parameters:
 *   Dispatcher* ActiveDispatcher - Dispatcher used to access console output.
 * Returns:
 *   void - No value is returned.
 */
void PrintSummaryIfNeeded(Dispatcher* ActiveDispatcher)
{
    if (State.SummaryPrinted)
    {
        return;
    }

    State.SummaryPrinted = true;
    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] summary: passed=%u failed=%u\n", State.TestsPassed, State.TestsFailed);
}

/**
 * Function: PrintMemoryTestResult
 * Description: Placeholder hook for detailed memory test result logging.
 * Parameters:
 *   Dispatcher* ActiveDispatcher - Dispatcher context for potential console output.
 * Returns:
 *   void - No value is returned.
 */
void PrintMemoryTestResult(Dispatcher* ActiveDispatcher)
{
    (void) ActiveDispatcher;
}

/**
 * Function: RunMemoryTest
 * Description: Runs physical allocation, virtual map/unmap, and heap allocation self-checks.
 * Parameters:
 *   Dispatcher* ActiveDispatcher - Dispatcher used to access resource managers.
 * Returns:
 *   bool - true if all memory checks pass; otherwise false.
 */
bool RunMemoryTest(Dispatcher* ActiveDispatcher)
{
    // Super-basic memory test:
    // 1) physical page allocate/free succeeds
    // 2) virtual page map/unmap API succeeds
    // 3) heap allocate/free succeeds
    ResourceLayer* Resource = ActiveDispatcher->GetResourceLayer();
    if (Resource == nullptr)
    {
        return false;
    }

    State.MemoryPhysicalOk = false;
    State.MemoryVirtualOk  = false;
    State.MemoryHeapOk     = false;

    void* PhysicalTestPage = Resource->GetPMM()->AllocatePagesFromDescriptor(1);
    if (PhysicalTestPage != nullptr)
    {
        State.MemoryPhysicalOk = Resource->GetPMM()->FreePagesFromDescriptor(PhysicalTestPage, 1);
    }

    // Virtual map/unmap smoke test.
    void* PhysicalForVirtualTest = Resource->GetPMM()->AllocatePagesFromDescriptor(1);
    if (PhysicalForVirtualTest != nullptr)
    {
        uint64_t TestVirtualAddr = (Resource->GetKernelHeapVirtualAddrEnd() + (512 * PAGE_SIZE)) & ~(PAGE_SIZE - 1);

        bool Mapped = Resource->GetVMM()->MapPage(reinterpret_cast<uint64_t>(PhysicalForVirtualTest), TestVirtualAddr, PageMappingFlags(false, true));
        if (Mapped)
        {
            bool Unmapped         = Resource->GetVMM()->UnmapPage(TestVirtualAddr);
            State.MemoryVirtualOk = Unmapped;
        }

        Resource->GetPMM()->FreePagesFromDescriptor(PhysicalForVirtualTest, 1);
    }

    void* HeapA        = Resource->kmalloc(128);
    void* HeapB        = Resource->kmalloc(512);
    State.MemoryHeapOk = (HeapA != nullptr && HeapB != nullptr);

    if (HeapA != nullptr)
    {
        Resource->kfree(HeapA);
    }
    if (HeapB != nullptr)
    {
        Resource->kfree(HeapB);
    }

    PrintMemoryTestResult(ActiveDispatcher);
    return State.MemoryPhysicalOk && State.MemoryVirtualOk && State.MemoryHeapOk;
}

/**
 * Function: KillAllTestProcessesExceptValidator
 * Description: Terminates all tracked self-test processes except the validator process.
 * Parameters:
 *   Dispatcher* ActiveDispatcher - Dispatcher used to issue process termination requests.
 * Returns:
 *   void - No value is returned.
 */
void KillAllTestProcessesExceptValidator(Dispatcher* ActiveDispatcher)
{
    for (uint8_t index = 0; index < State.TestProcessCount; ++index)
    {
        uint8_t ProcessId = State.TestProcessIds[index];
        if (ProcessId == INVALID_PROCESS_ID || ProcessId == State.KernelValidatorId)
        {
            continue;
        }

        ActiveDispatcher->GetLogicLayer()->KillProcess(ProcessId);
    }

    State.TestProcessCount = 0;
    RegisterTestProcess(State.KernelValidatorId);
    State.KernelSleepFastId   = INVALID_PROCESS_ID;
    State.KernelSleepMediumId = INVALID_PROCESS_ID;
    State.KernelSleepSlowId   = INVALID_PROCESS_ID;
    State.KernelBurstId       = INVALID_PROCESS_ID;
}

/**
 * Function: SetupMultitaskingTest
 * Description: Creates kernel and user test processes required for multitasking and syscall validation.
 * Parameters:
 *   Dispatcher* ActiveDispatcher - Dispatcher used to create and register test processes.
 * Returns:
 *   bool - true if all required processes are created successfully; otherwise false.
 */
bool SetupMultitaskingTest(Dispatcher* ActiveDispatcher)
{
    ResetMultitaskCounters();

    State.KernelSleepFastId   = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelSleepFastTask);
    State.KernelSleepMediumId = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelSleepMediumTask);
    State.KernelSleepSlowId   = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelSleepSlowTask);
    State.KernelBurstId       = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelBurstTask);

    if (State.KernelSleepFastId == INVALID_PROCESS_ID || State.KernelSleepMediumId == INVALID_PROCESS_ID || State.KernelSleepSlowId == INVALID_PROCESS_ID || State.KernelBurstId == INVALID_PROCESS_ID)
    {
        return false;
    }

    RegisterTestProcess(State.KernelSleepFastId);
    RegisterTestProcess(State.KernelSleepMediumId);
    RegisterTestProcess(State.KernelSleepSlowId);
    RegisterTestProcess(State.KernelBurstId);

    for (uint32_t index = 0; index < USER_PROCESS_INSTANCE_COUNT; ++index)
    {
        if (CreateUserProcessFromInitramfs(ActiveDispatcher, "/init", false) == INVALID_PROCESS_ID)
        {
            return false;
        }

        if (CreateUserProcessFromInitramfs(ActiveDispatcher, "/init2", true) == INVALID_PROCESS_ID)
        {
            return false;
        }
    }
    return true;
}

/**
 * Function: CreateUserProcessFromInitramfs
 * Description: Loads a user image from initramfs, validates format policy, and creates a user process.
 * Parameters:
 *   Dispatcher* ActiveDispatcher - Dispatcher used for file loading, logging, and process creation.
 *   const char* Path - Initramfs path to the user image.
 *   bool RequireElfImage - Whether image must validate as ELF before process creation.
 * Returns:
 *   uint8_t - Created process ID, or INVALID_PROCESS_ID on failure.
 */
uint8_t CreateUserProcessFromInitramfs(Dispatcher* ActiveDispatcher, const char* Path, bool RequireElfImage)
{
    uint64_t FileSize = 0;
    void*    FileData = ActiveDispatcher->GetResourceLayer()->LoadFileFromInitramfs(Path, &FileSize);

    if (FileData == nullptr || FileSize == 0)
    {
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] [UserCreate] load failed path=%s size=%llu\n", Path, (unsigned long long) FileSize);
        return INVALID_PROCESS_ID;
    }

    bool ElfImage = ValidateImageAsElf(ActiveDispatcher, FileData, FileSize);
    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] [UserCreate] image path=%s size=%llu format=%s expected=%s\n", Path, (unsigned long long) FileSize,
                                                                ElfImage ? "elf" : "raw-binary", RequireElfImage ? "elf" : "raw-or-elf");

    if (RequireElfImage && !ElfImage)
    {
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] [UserCreate] rejected non-elf image path=%s\n", Path);
        return INVALID_PROCESS_ID;
    }

    uint8_t UserProcessId = ActiveDispatcher->GetLogicLayer()->CreateUserProcess(reinterpret_cast<uint64_t>(FileData), static_cast<uint64_t>(FileSize));
    if (UserProcessId == INVALID_PROCESS_ID)
    {
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] [UserCreate] create user process failed path=%s\n", Path);
        return INVALID_PROCESS_ID;
    }

    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] [UserCreate] created user process path=%s pid=%u\n", Path, (unsigned) UserProcessId);

    RegisterTestProcess(UserProcessId);

    return UserProcessId;
}

} // namespace

/**
 * Function: KernelSelfTestStart
 * Description: Initializes global self-test state and spawns the validator kernel process.
 * Parameters:
 *   Dispatcher* ActiveDispatcher - Dispatcher used to create and manage self-test processes.
 * Returns:
 *   bool - true if self-test setup succeeds; otherwise false.
 */
bool KernelSelfTestStart(Dispatcher* ActiveDispatcher)
{
    if (ActiveDispatcher == nullptr)
    {
        return false;
    }

    State                     = {};
    State.DispatcherRef       = ActiveDispatcher;
    State.KernelSleepFastId   = INVALID_PROCESS_ID;
    State.KernelSleepMediumId = INVALID_PROCESS_ID;
    State.KernelSleepSlowId   = INVALID_PROCESS_ID;
    State.KernelBurstId       = INVALID_PROCESS_ID;
    State.KernelValidatorId   = INVALID_PROCESS_ID;
    State.Phase               = SELF_TEST_PHASE_MEMORY;
    State.Initialized         = true;
    State.Passed              = false;

    State.KernelValidatorId = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelValidatorTask);

    if (State.KernelValidatorId == INVALID_PROCESS_ID)
    {
        return false;
    }

    RegisterTestProcess(State.KernelValidatorId);

    return true;
}

/**
 * Function: KernelSelfTestsOnSystemCall
 * Description: Records syscall activity counters while multitasking self-tests are in monitor phase.
 * Parameters:
 *   uint64_t SystemCallNumber - System call identifier observed by the kernel hook.
 * Returns:
 *   void - No value is returned.
 */
void KernelSelfTestsOnSystemCall(uint64_t SystemCallNumber)
{
    if (!State.Initialized || State.Passed || State.Phase != SELF_TEST_PHASE_MULTITASK_MONITOR)
    {
        return;
    }

    if (SystemCallNumber == 1)
    {
        ++State.SyscallOneCount;
    }
    else if (SystemCallNumber == 2)
    {
        ++State.SyscallTwoCount;
    }
}

/**
 * Function: KernelValidatorTask
 * Description: Drives self-test phase transitions, logs progress/results, and manages test lifecycle.
 * Parameters:
 *   None - This function takes no parameters.
 * Returns:
 *   void - No value is returned.
 */
void KernelValidatorTask()
{
    // Validator owns the test lifecycle and can be extended to run more suites
    // in sequence. It intentionally drives setup/monitor/teardown centrally.
    Dispatcher* ActiveDispatcher = RequireDispatcher();
    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] suite start: validator process running and monitoring test cases\n");
    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] test cases: Memory | ELF and Raw Binary User creation (syscall validation) | Multitasking and Sleep\n");

    while (1)
    {
        switch (State.Phase)
        {
            case SELF_TEST_PHASE_MEMORY:
            {
                // Run memory suite first; multitasking suite is skipped on failure.
                ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] [Memory] test started\n");
                if (!RunMemoryTest(ActiveDispatcher))
                {
                    LogTestResult(ActiveDispatcher, "Memory", false);
                    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] memory details: physical=%s virtual=%s heap=%s\n", State.MemoryPhysicalOk ? "ok" : "fail",
                                                                                State.MemoryVirtualOk ? "ok" : "fail", State.MemoryHeapOk ? "ok" : "fail");
                    State.Phase = SELF_TEST_PHASE_FAILED;
                    break;
                }

                LogTestResult(ActiveDispatcher, "Memory", true);
                State.Phase = SELF_TEST_PHASE_MULTITASK_SETUP;
            }
            break;

            case SELF_TEST_PHASE_MULTITASK_SETUP:
            {
                // Spawn runtime actors for user-process creation and scheduling checks.
                ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] [ELF and Raw Binary User creation] test started (includes syscall instruction validation)\n");
                ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] [Multitasking and Sleep] test started\n");
                if (!SetupMultitaskingTest(ActiveDispatcher))
                {
                    LogTestResult(ActiveDispatcher, "ELF and Raw Binary User creation", false);
                    LogTestResult(ActiveDispatcher, "Multitasking and Sleep", false);
                    State.Phase = SELF_TEST_PHASE_FAILED;
                    break;
                }

                ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] [Multitasking and Sleep] monitoring: burst target=%llu\n", (unsigned long long) BURST_MIN_LOOPS);

                State.Phase = SELF_TEST_PHASE_MULTITASK_MONITOR;
            }
            break;

            case SELF_TEST_PHASE_MULTITASK_MONITOR:
            {
                // Observe counters from kernel sleepers, burst task, and user syscalls.
                if (State.BurstLoops >= State.NextMultitaskProgressBurstLoops)
                {
                    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_(
                            "[SelfTest] [Multitasking and Sleep] progress: fast=%llu medium=%llu slow=%llu burst=%llu/%llu syscall1=%llu/%u syscall2=%llu/%u\n", (unsigned long long) State.FastCycles,
                            (unsigned long long) State.MediumCycles, (unsigned long long) State.SlowCycles, (unsigned long long) State.BurstLoops, (unsigned long long) BURST_MIN_LOOPS,
                            (unsigned long long) State.SyscallOneCount, (unsigned) USER_PROCESS_INSTANCE_COUNT, (unsigned long long) State.SyscallTwoCount, (unsigned) USER_PROCESS_INSTANCE_COUNT);

                    State.NextMultitaskProgressBurstLoops += 200;
                }

                if (UserChecksPassed() && !State.UserCreationResultLogged)
                {
                    LogTestResult(ActiveDispatcher, "ELF and Raw Binary User creation", true);
                    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] syscall instruction validation: syscall1=%llu syscall2=%llu\n", (unsigned long long) State.SyscallOneCount,
                                                                                (unsigned long long) State.SyscallTwoCount);
                    State.UserCreationResultLogged = true;
                }

                if (KernelChecksPassed() && !State.MultitaskSleepResultLogged)
                {
                    LogTestResult(ActiveDispatcher, "Multitasking and Sleep", true);
                    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] multitasking and sleep details: fast=%llu medium=%llu slow=%llu burst=%llu\n",
                                                                                (unsigned long long) State.FastCycles, (unsigned long long) State.MediumCycles, (unsigned long long) State.SlowCycles,
                                                                                (unsigned long long) State.BurstLoops);
                    State.MultitaskSleepResultLogged = true;
                }

                if (State.UserCreationResultLogged && State.MultitaskSleepResultLogged)
                {
                    KillAllTestProcessesExceptValidator(ActiveDispatcher);
                    State.Passed = true;
                    State.Phase  = SELF_TEST_PHASE_COMPLETE;
                }
            }
            break;

            case SELF_TEST_PHASE_COMPLETE:
            {
                // Place-holder transition point for future suites after multitasking.
                PrintSummaryIfNeeded(ActiveDispatcher);
                State.Phase = SELF_TEST_PHASE_FAILED;
            }
            break;

            case SELF_TEST_PHASE_FAILED:
            default:
            {
                PrintSummaryIfNeeded(ActiveDispatcher);
            }
            break;
        }

        ActiveDispatcher->GetLogicLayer()->SleepProcess(State.KernelValidatorId, VALIDATOR_SLEEP_TICKS);
    }
}
