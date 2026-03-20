/**
 * File: KernelSelfTests.cpp
 * Author: Marwan Mostafa
 * Description: Kernel self-test implementations.
 */

#include "KernelSelfTests.hpp"

#include <CommonUtils.hpp>
#include <Layers/Dispatcher.hpp>

void KernelValidatorTask();

namespace
{
constexpr uint8_t INVALID_PROCESS_ID = 0xFF;

constexpr bool ENABLE_MEMORY_TEST    = false;
constexpr bool ENABLE_MULTITASK_TEST = false;
constexpr bool ENABLE_USER_MODE_TEST = true;

constexpr uint64_t KERNEL_SLEEP_FAST_TICKS   = 10;
constexpr uint64_t KERNEL_SLEEP_MEDIUM_TICKS = 300;
constexpr uint64_t KERNEL_SLEEP_SLOW_TICKS   = 800;
constexpr uint64_t KERNEL_BURST_SLEEP_TICKS  = 1;
constexpr uint64_t VALIDATOR_SLEEP_TICKS     = 10;

constexpr uint32_t TEST_MAX_PROCESS_COUNT = 32;

constexpr uint64_t FAST_MIN_CYCLES   = 4;
constexpr uint64_t MEDIUM_MIN_CYCLES = 3;
constexpr uint64_t SLOW_MIN_CYCLES   = 2;
constexpr uint64_t BURST_MIN_LOOPS   = 500;

// 30 validator sleep iterations × 100 ms each = 3 seconds.
constexpr uint64_t USER_MODE_TEST_DURATION_LOOPS = 30;

enum SelfTestPhase
{
    // Phase 1: run memory-focused checks before any heavy multitasking load.
    SELF_TEST_PHASE_MEMORY = 0,
    // Phase 2: spawn kernel/user processes required for scheduling validation.
    SELF_TEST_PHASE_MULTITASK_SETUP,
    // Phase 3: monitor runtime counters until pass criteria are satisfied.
    SELF_TEST_PHASE_MULTITASK_MONITOR,
    // Phase 4: spawn the execve-chaining user mode test process.
    SELF_TEST_PHASE_USER_MODE_SETUP,
    // Phase 5: wait 3 seconds then kill the forked child process.
    SELF_TEST_PHASE_USER_MODE_MONITOR,
    // Phase 6: all suites completed successfully; print summary then idle.
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

    uint64_t NextMultitaskProgressBurstLoops;

    bool MemoryVirtualOk;
    bool MemoryPhysicalOk;
    bool MemoryHeapOk;
    bool MultitaskSleepResultLogged;

    uint8_t  UserModeTestProcessId;
    uint64_t UserModeTestLoopCount;
    bool     UserModeTestResultLogged;

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
        false,
        false,
        false,
        false,
        INVALID_PROCESS_ID,
        0,
        false,
        0,
        0,
        false,
        SELF_TEST_PHASE_MEMORY,
        false,
        false,
};

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
    State.NextMultitaskProgressBurstLoops = 200;
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

    ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [%s] test result=%s\n", TestName, Passed ? "PASS" : "FAIL");
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
    ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] summary: passed=%u failed=%u\n", State.TestsPassed, State.TestsFailed);
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

uint8_t KillFirstChildProcessOf(Dispatcher* ActiveDispatcher, uint8_t ParentProcessId)
{
    if (ActiveDispatcher == nullptr || ParentProcessId == INVALID_PROCESS_ID)
    {
        return INVALID_PROCESS_ID;
    }

    ProcessManager* PM = ActiveDispatcher->GetLogicLayer()->GetProcessManager();
    if (PM == nullptr)
    {
        return INVALID_PROCESS_ID;
    }

    Process* RunningProcess   = PM->GetRunningProcess();
    uint8_t  RunningProcessId = (RunningProcess != nullptr) ? RunningProcess->Id : INVALID_PROCESS_ID;

    for (uint8_t ProcessId = 0; ProcessId < TEST_MAX_PROCESS_COUNT; ++ProcessId)
    {
        Process* Candidate = PM->GetProcessById(ProcessId);
        if (Candidate == nullptr || Candidate->Status == PROCESS_TERMINATED)
        {
            continue;
        }

        if (Candidate->ParrentId == ParentProcessId)
        {
            if (ProcessId == State.KernelValidatorId || ProcessId == RunningProcessId)
            {
                continue;
            }

            ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [User Mode] terminating child process id=%u of parent id=%u\n", ProcessId, ParentProcessId);
            ActiveDispatcher->GetLogicLayer()->KillProcess(ProcessId);
            return ProcessId;
        }
    }

    return INVALID_PROCESS_ID;
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

    return true;
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

    State                       = {};
    State.DispatcherRef         = ActiveDispatcher;
    State.KernelSleepFastId     = INVALID_PROCESS_ID;
    State.KernelSleepMediumId   = INVALID_PROCESS_ID;
    State.KernelSleepSlowId     = INVALID_PROCESS_ID;
    State.KernelBurstId         = INVALID_PROCESS_ID;
    State.KernelValidatorId     = INVALID_PROCESS_ID;
    State.UserModeTestProcessId = INVALID_PROCESS_ID;
    State.Phase                 = SELF_TEST_PHASE_MEMORY;
    State.Initialized           = true;
    State.Passed                = false;

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
    (void) SystemCallNumber;
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
    ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] suite start: validator process running and monitoring test cases\n");
    ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] test cases: Memory | Multitasking and Sleep | User Mode\n");

    while (1)
    {
        switch (State.Phase)
        {
            case SELF_TEST_PHASE_MEMORY:
            {
                if (!ENABLE_MEMORY_TEST)
                {
                    ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [Memory] test skipped (disabled)\n");
                    State.Phase = SELF_TEST_PHASE_MULTITASK_SETUP;
                    break;
                }

                // Run memory suite first; multitasking suite is skipped on failure.
                ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [Memory] test started\n");
                if (!RunMemoryTest(ActiveDispatcher))
                {
                    LogTestResult(ActiveDispatcher, "Memory", false);
                    ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] memory details: physical=%s virtual=%s heap=%s\n", State.MemoryPhysicalOk ? "ok" : "fail",
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
                if (!ENABLE_MULTITASK_TEST)
                {
                    ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [Multitasking and Sleep] test skipped (disabled)\n");
                    State.Phase = SELF_TEST_PHASE_USER_MODE_SETUP;
                    break;
                }

                // Spawn runtime actors for user-process creation and scheduling checks.
                ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [Multitasking and Sleep] test started\n");
                if (!SetupMultitaskingTest(ActiveDispatcher))
                {
                    LogTestResult(ActiveDispatcher, "Multitasking and Sleep", false);
                    State.Phase = SELF_TEST_PHASE_FAILED;
                    break;
                }

                ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [Multitasking and Sleep] monitoring: burst target=%llu\n", (unsigned long long) BURST_MIN_LOOPS);

                State.Phase = SELF_TEST_PHASE_MULTITASK_MONITOR;
            }
            break;

            case SELF_TEST_PHASE_MULTITASK_MONITOR:
            {
                // Observe counters from kernel sleepers and burst task.
                if (State.BurstLoops >= State.NextMultitaskProgressBurstLoops)
                {
                    ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [Multitasking and Sleep] progress: fast=%llu medium=%llu slow=%llu burst=%llu/%llu\n",
                                                                            (unsigned long long) State.FastCycles, (unsigned long long) State.MediumCycles, (unsigned long long) State.SlowCycles,
                                                                            (unsigned long long) State.BurstLoops, (unsigned long long) BURST_MIN_LOOPS);

                    State.NextMultitaskProgressBurstLoops += 200;
                }

                if (KernelChecksPassed() && !State.MultitaskSleepResultLogged)
                {
                    LogTestResult(ActiveDispatcher, "Multitasking and Sleep", true);
                    ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] multitasking and sleep details: fast=%llu medium=%llu slow=%llu burst=%llu\n",
                                                                            (unsigned long long) State.FastCycles, (unsigned long long) State.MediumCycles, (unsigned long long) State.SlowCycles,
                                                                            (unsigned long long) State.BurstLoops);
                    State.MultitaskSleepResultLogged = true;
                }

                if (State.MultitaskSleepResultLogged)
                {
                    KillAllTestProcessesExceptValidator(ActiveDispatcher);
                    State.Phase = SELF_TEST_PHASE_USER_MODE_SETUP;
                }
            }
            break;

            case SELF_TEST_PHASE_COMPLETE:
            {
                // All suites passed; print summary then idle in FAILED.
                PrintSummaryIfNeeded(ActiveDispatcher);
                State.Phase = SELF_TEST_PHASE_FAILED;
            }
            break;

            case SELF_TEST_PHASE_USER_MODE_SETUP:
            {
                if (!ENABLE_USER_MODE_TEST)
                {
                    ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [User Mode] test skipped (disabled)\n");
                    State.Passed = true;
                    State.Phase  = SELF_TEST_PHASE_COMPLETE;
                    break;
                }

                // Spawn a single user process at /Test1; it will fork and the child
                // will execve into /Test2, which writes to TTY.
                ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [User Mode] test started: /Test1 will fork and child execve /Test2 for 3 seconds\n");
                State.UserModeTestProcessId = ActiveDispatcher->GetLogicLayer()->CreateUserProcessFromVFS("/Test1");
                if (State.UserModeTestProcessId == INVALID_PROCESS_ID)
                {
                    LogTestResult(ActiveDispatcher, "User Mode", false);
                    State.Phase = SELF_TEST_PHASE_FAILED;
                    break;
                }

                if (State.UserModeTestProcessId == State.KernelValidatorId)
                {
                    ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [User Mode] pid collision detected: validator id=%u\n", State.KernelValidatorId);
                    LogTestResult(ActiveDispatcher, "User Mode", false);
                    State.UserModeTestProcessId = INVALID_PROCESS_ID;
                    State.Phase                 = SELF_TEST_PHASE_FAILED;
                    break;
                }

                RegisterTestProcess(State.UserModeTestProcessId);
                State.UserModeTestLoopCount = 0;
                State.Phase                 = SELF_TEST_PHASE_USER_MODE_MONITOR;
            }
            break;

            case SELF_TEST_PHASE_USER_MODE_MONITOR:
            {
                // Each validator wake-up is ~100 ms; after 30 iterations (~3 s) terminate the
                // forked child and verify parent wait() can resume.
                ++State.UserModeTestLoopCount;

                if (State.UserModeTestLoopCount >= USER_MODE_TEST_DURATION_LOOPS && !State.UserModeTestResultLogged)
                {
                    uint8_t         ParentUserProcessId = State.UserModeTestProcessId;
                    ProcessManager* PM                  = ActiveDispatcher->GetLogicLayer()->GetProcessManager();
                    Process*        RunningProcess      = (PM != nullptr) ? PM->GetRunningProcess() : nullptr;
                    uint8_t         RunningProcessId    = (RunningProcess != nullptr) ? RunningProcess->Id : INVALID_PROCESS_ID;

                    if (ParentUserProcessId == INVALID_PROCESS_ID)
                    {
                        ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [User Mode] invalid parent process id\n");
                        LogTestResult(ActiveDispatcher, "User Mode", false);
                        State.Phase = SELF_TEST_PHASE_FAILED;
                        break;
                    }

                    if (ParentUserProcessId == State.KernelValidatorId || ParentUserProcessId == RunningProcessId)
                    {
                        ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [User Mode] refusing to target active validator/running process id=%u\n", ParentUserProcessId);
                        LogTestResult(ActiveDispatcher, "User Mode", false);
                        State.Phase = SELF_TEST_PHASE_FAILED;
                        break;
                    }

                    uint8_t KilledChildId = KillFirstChildProcessOf(ActiveDispatcher, ParentUserProcessId);
                    if (KilledChildId == INVALID_PROCESS_ID)
                    {
                        ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [User Mode] no child process found for parent id=%u\n", ParentUserProcessId);
                        LogTestResult(ActiveDispatcher, "User Mode", false);
                        State.Phase = SELF_TEST_PHASE_FAILED;
                        break;
                    }

                    LogTestResult(ActiveDispatcher, "User Mode", true);
                    ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("[SelfTest] [User Mode] child id=%u terminated by validator; parent id=%u should wake from wait()\n", KilledChildId,
                                                                            ParentUserProcessId);
                    State.UserModeTestResultLogged = true;
                    State.Passed                   = true;
                    State.Phase                    = SELF_TEST_PHASE_COMPLETE;
                }
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
