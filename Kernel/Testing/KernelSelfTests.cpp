#include "KernelSelfTests.hpp"

#include <Layers/Dispatcher.hpp>

#include <CommonUtils.hpp>

void KernelValidatorTask();

namespace
{
constexpr uint8_t INVALID_PROCESS_ID = 0xFF;

constexpr uint64_t KERNEL_SLEEP_FAST_TICKS   = 4;
constexpr uint64_t KERNEL_SLEEP_MEDIUM_TICKS = 10;
constexpr uint64_t KERNEL_SLEEP_SLOW_TICKS   = 22;
constexpr uint64_t KERNEL_BURST_SLEEP_TICKS  = 1;
constexpr uint64_t VALIDATOR_SLEEP_TICKS     = 6;

constexpr uint32_t USER_PROCESS_INSTANCE_COUNT = 1;
constexpr uint32_t TEST_MAX_PROCESS_COUNT      = 32;

constexpr uint64_t FAST_MIN_CYCLES   = 1;
constexpr uint64_t MEDIUM_MIN_CYCLES = 1;
constexpr uint64_t SLOW_MIN_CYCLES   = 1;
constexpr uint64_t BURST_MIN_LOOPS   = 600;

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

    bool MemoryVirtualOk;
    bool MemoryPhysicalOk;
    bool MemoryHeapOk;

    SelfTestPhase Phase;
    bool Initialized;
    bool Passed;
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
        false,
        false,
        false,
        SELF_TEST_PHASE_MEMORY,
        false,
        false,
};

    uint8_t CreateUserProcessFromInitramfs(Dispatcher* ActiveDispatcher, const char* Path);

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

void ResetMultitaskCounters()
{
    State.FastCycles     = 0;
    State.MediumCycles   = 0;
    State.SlowCycles     = 0;
    State.BurstLoops     = 0;
    State.SyscallOneCount = 0;
    State.SyscallTwoCount = 0;
}

void KernelSleepFastTask()
{
    Dispatcher* ActiveDispatcher = RequireDispatcher();

    while (1)
    {
        ++State.FastCycles;

        if ((State.FastCycles % 8) == 0)
        {
            ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/KFast] cycle=%llu\n", (unsigned long long) State.FastCycles);
        }

        ActiveDispatcher->GetLogicLayer()->SleepProcess(State.KernelSleepFastId, KERNEL_SLEEP_FAST_TICKS);
    }
}

void KernelSleepMediumTask()
{
    Dispatcher* ActiveDispatcher = RequireDispatcher();

    while (1)
    {
        ++State.MediumCycles;

        if ((State.MediumCycles % 5) == 0)
        {
            ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/KMedium] cycle=%llu\n", (unsigned long long) State.MediumCycles);
        }

        ActiveDispatcher->GetLogicLayer()->SleepProcess(State.KernelSleepMediumId, KERNEL_SLEEP_MEDIUM_TICKS);
    }
}

void KernelSleepSlowTask()
{
    Dispatcher* ActiveDispatcher = RequireDispatcher();

    while (1)
    {
        ++State.SlowCycles;

        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/KSlow] cycle=%llu\n", (unsigned long long) State.SlowCycles);

        ActiveDispatcher->GetLogicLayer()->SleepProcess(State.KernelSleepSlowId, KERNEL_SLEEP_SLOW_TICKS);
    }
}

void KernelBurstTask()
{
    Dispatcher* ActiveDispatcher = RequireDispatcher();

    while (1)
    {
        ++State.BurstLoops;

        if ((State.BurstLoops % 300) == 0)
        {
            ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/KBurst] loops=%llu\n", (unsigned long long) State.BurstLoops);
        }

        if ((State.BurstLoops % 150) == 0)
        {
            ActiveDispatcher->GetLogicLayer()->SleepProcess(State.KernelBurstId, KERNEL_BURST_SLEEP_TICKS);
        }

        __asm__ __volatile__("pause");
    }
}

bool KernelChecksPassed()
{
    return State.FastCycles >= FAST_MIN_CYCLES && State.MediumCycles >= MEDIUM_MIN_CYCLES && State.SlowCycles >= SLOW_MIN_CYCLES && State.BurstLoops >= BURST_MIN_LOOPS;
}

bool UserChecksPassed()
{
    return State.SyscallOneCount >= USER_PROCESS_INSTANCE_COUNT && State.SyscallTwoCount >= USER_PROCESS_INSTANCE_COUNT;
}

void PrintMemoryTestResult(Dispatcher* ActiveDispatcher)
{
    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_(
            "[SelfTest/Memory] physical=%s virtual=%s heap=%s\n", State.MemoryPhysicalOk ? "ok" : "fail", State.MemoryVirtualOk ? "ok" : "fail", State.MemoryHeapOk ? "ok" : "fail");
}

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

        bool Mapped = Resource->GetVMM()->MapPage(reinterpret_cast<uint64_t>(PhysicalForVirtualTest), TestVirtualAddr, false);
        if (Mapped)
        {
            bool Unmapped = Resource->GetVMM()->UnmapPage(TestVirtualAddr);
            State.MemoryVirtualOk = Unmapped;

            ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/Memory] map/unmap va=%p pa=%p mapped=%s unmapped=%s\n", (void*) TestVirtualAddr,
                                                                         PhysicalForVirtualTest, Mapped ? "ok" : "fail", Unmapped ? "ok" : "fail");
        }
        else
        {
            ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/Memory] map failed va=%p pa=%p\n", (void*) TestVirtualAddr, PhysicalForVirtualTest);
        }

        Resource->GetPMM()->FreePagesFromDescriptor(PhysicalForVirtualTest, 1);
    }

    void* HeapA = Resource->kmalloc(128);
    void* HeapB = Resource->kmalloc(512);
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
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/Validator] killed process id=%u\n", ProcessId);
    }

    State.TestProcessCount = 0;
    RegisterTestProcess(State.KernelValidatorId);
    State.KernelSleepFastId   = INVALID_PROCESS_ID;
    State.KernelSleepMediumId = INVALID_PROCESS_ID;
    State.KernelSleepSlowId   = INVALID_PROCESS_ID;
    State.KernelBurstId       = INVALID_PROCESS_ID;
}

bool SetupMultitaskingTest(Dispatcher* ActiveDispatcher)
{
    ResetMultitaskCounters();

    State.KernelSleepFastId   = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelSleepFastTask);
    State.KernelSleepMediumId = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelSleepMediumTask);
    State.KernelSleepSlowId   = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelSleepSlowTask);
    State.KernelBurstId       = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelBurstTask);

    if (State.KernelSleepFastId == INVALID_PROCESS_ID || State.KernelSleepMediumId == INVALID_PROCESS_ID || State.KernelSleepSlowId == INVALID_PROCESS_ID ||
        State.KernelBurstId == INVALID_PROCESS_ID)
    {
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] failed to create kernel multitask test processes\n");
        return false;
    }

    RegisterTestProcess(State.KernelSleepFastId);
    RegisterTestProcess(State.KernelSleepMediumId);
    RegisterTestProcess(State.KernelSleepSlowId);
    RegisterTestProcess(State.KernelBurstId);

    for (uint32_t index = 0; index < USER_PROCESS_INSTANCE_COUNT; ++index)
    {
        if (CreateUserProcessFromInitramfs(ActiveDispatcher, "/init") == INVALID_PROCESS_ID)
        {
            return false;
        }

        if (CreateUserProcessFromInitramfs(ActiveDispatcher, "/init2") == INVALID_PROCESS_ID)
        {
            return false;
        }

        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] created user pair %u/%u\n", (unsigned) (index + 1), (unsigned) USER_PROCESS_INSTANCE_COUNT);
    }

    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] multitasking test armed\n");
    return true;
}

uint8_t CreateUserProcessFromInitramfs(Dispatcher* ActiveDispatcher, const char* Path)
{
    uint64_t FileSize = 0;
    void*    FileData = ActiveDispatcher->GetResourceLayer()->LoadFileFromInitramfs(Path, &FileSize);

    if (FileData == nullptr || FileSize == 0)
    {
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] failed to load %s\n", Path);
        return INVALID_PROCESS_ID;
    }

    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] %s loaded at %p (%llu bytes)\n", Path, FileData, (unsigned long long) FileSize);

    uint8_t UserProcessId = ActiveDispatcher->GetLogicLayer()->CreateUserProcess(reinterpret_cast<uint64_t>(FileData), static_cast<uint64_t>(FileSize));
    if (UserProcessId == INVALID_PROCESS_ID)
    {
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] failed to create process for %s\n", Path);
        return INVALID_PROCESS_ID;
    }

    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] created process for %s id=%u\n", Path, UserProcessId);

    RegisterTestProcess(UserProcessId);

    return UserProcessId;
}

} // namespace

bool KernelMultiTaskingTest(Dispatcher* ActiveDispatcher)
{
    if (ActiveDispatcher == nullptr)
    {
        return false;
    }

    State = {
            ActiveDispatcher,
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
            false,
            false,
            false,
            SELF_TEST_PHASE_MEMORY,
            true,
            false,
    };

    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] starting validator-managed test suite\n");

    State.KernelValidatorId   = ActiveDispatcher->GetLogicLayer()->CreateKernelProcess(KernelValidatorTask);

    if (State.KernelValidatorId == INVALID_PROCESS_ID)
    {
        ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] failed to create validator process\n");
        return false;
    }

    RegisterTestProcess(State.KernelValidatorId);
    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest] validator created: id=%u\n", State.KernelValidatorId);

    return true;
}

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

void KernelValidatorTask()
{
    // Validator owns the test lifecycle and can be extended to run more suites
    // in sequence. It intentionally drives setup/monitor/teardown centrally.
    Dispatcher* ActiveDispatcher = RequireDispatcher();

    while (1)
    {
        switch (State.Phase)
        {
            case SELF_TEST_PHASE_MEMORY:
            {
                // Run memory suite first; multitasking suite is skipped on failure.
                ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/Validator] phase=memory\n");
                if (!RunMemoryTest(ActiveDispatcher))
                {
                    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/Validator] FAIL: memory test\n");
                    State.Phase = SELF_TEST_PHASE_FAILED;
                    break;
                }

                ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/Validator] PASS: memory test\n");
                State.Phase = SELF_TEST_PHASE_MULTITASK_SETUP;
            }
            break;

            case SELF_TEST_PHASE_MULTITASK_SETUP:
            {
                // Spawn all runtime actors needed to stress scheduler behavior.
                ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/Validator] phase=multitask-setup\n");
                if (!SetupMultitaskingTest(ActiveDispatcher))
                {
                    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/Validator] FAIL: multitask setup\n");
                    State.Phase = SELF_TEST_PHASE_FAILED;
                    break;
                }

                State.Phase = SELF_TEST_PHASE_MULTITASK_MONITOR;
            }
            break;

            case SELF_TEST_PHASE_MULTITASK_MONITOR:
            {
                // Observe counters from kernel sleepers, burst task, and user syscalls.
                ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_(
                        "[SelfTest/Validator] fast=%llu medium=%llu slow=%llu burst=%llu syscall1=%llu syscall2=%llu\n", (unsigned long long) State.FastCycles,
                        (unsigned long long) State.MediumCycles, (unsigned long long) State.SlowCycles, (unsigned long long) State.BurstLoops,
                        (unsigned long long) State.SyscallOneCount, (unsigned long long) State.SyscallTwoCount);

                if (KernelChecksPassed() && UserChecksPassed())
                {
                    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/Validator] PASS: kernel/user/sleep checks satisfied\n");
                    KillAllTestProcessesExceptValidator(ActiveDispatcher);
                    ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/Validator] all test processes terminated\n");
                    State.Passed = true;
                    State.Phase  = SELF_TEST_PHASE_COMPLETE;
                }
            }
            break;

            case SELF_TEST_PHASE_COMPLETE:
            {
                // Place-holder transition point for future suites after multitasking.
                ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/Validator] all enabled tests complete, ready for next test\n");
                State.Phase = SELF_TEST_PHASE_FAILED;
            }
            break;

            case SELF_TEST_PHASE_FAILED:
            default:
            {
                ActiveDispatcher->GetResourceLayer()->GetConsole()->printf_("[SelfTest/Validator] idle\n");
            }
            break;
        }

        ActiveDispatcher->GetLogicLayer()->SleepProcess(State.KernelValidatorId, VALIDATOR_SLEEP_TICKS);
    }
}
