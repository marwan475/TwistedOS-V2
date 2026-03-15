#include "LogicLayer.hpp"

#include "Layers/Resource/ResourceLayer.hpp"

namespace
{
void NullProcessEntry()
{
    while (1)
        __asm__ __volatile__("hlt");
}
} // namespace

LogicLayer::LogicLayer() : Resource(nullptr), PM(nullptr), Sched(nullptr)
{
}

LogicLayer::~LogicLayer()
{
    if (PM != nullptr)
    {
        delete PM;
    }

    if (Sched != nullptr)
    {
        delete Sched;
    }

    if (Sync != nullptr)
    {
        delete Sync;
    }
}

void LogicLayer::Initialize(ResourceLayer* Resource)
{
    this->Resource = Resource;
}

ResourceLayer* LogicLayer::GetResourceLayer() const
{
    return Resource;
}

void LogicLayer::InitializeProcessManager()
{
    if (PM != nullptr)
    {
        return;
    }

    PM = new ProcessManager();
    Resource->GetConsole()->printf_("Process Manager Initialized\n");
}

void LogicLayer::InitializeScheduler()
{
    if (Sched != nullptr)
    {
        return;
    }

    Sched = new Scheduler();
    Resource->GetConsole()->printf_("Scheduler Initialized\n");
}

void LogicLayer::InitializeSynchronizationManager()
{
    if (Sync != nullptr)
    {
        return;
    }

    Sync = new SynchronizationManager();
    Resource->GetConsole()->printf_("Synchronization Manager Initialized\n");
}

uint8_t LogicLayer::CreateNullProcess()
{
    uint8_t Id = CreateKernelProcess(NullProcessEntry);

    Process* NullProcess = PM->GetProcessById(Id);
    NullProcess->Status  = PROCESS_RUNNING; // So RunProcess works on first schedule

    return Id;
}

uint8_t LogicLayer::CreateKernelProcess(void (*EntryPoint)())
{
    void*    ProcessStack = Resource->kmalloc(KERNEL_PROCESS_STACK_SIZE);
    uint64_t StackTop     = reinterpret_cast<uint64_t>(ProcessStack) + KERNEL_PROCESS_STACK_SIZE;

    CpuState State = {};
    State.rip      = reinterpret_cast<uint64_t>(EntryPoint);
    State.rflags   = 0x202;                    // Bit1 always set, IF enabled
    State.rbp      = 0;                        // bottom of stack frame
    State.rsp      = (StackTop & ~0xFULL) - 8; // SysV entry alignment without a real CALL

    *reinterpret_cast<uint64_t*>(State.rsp) = 0;

    State.cs   = KERNEL_CS;
    State.ss   = KERNEL_SS;
    uint8_t Id = PM->CreateKernelProcess(ProcessStack, State);

    if (Id != 0xFF)
    {
        Sched->AddToReadyQueue(Id);
    }

    return Id;
}

uint8_t LogicLayer::CreateUserProcess(uint64_t CodeAddr, uint64_t CodeSize)
{
    (void) CodeSize;

    void* ProcessStack = Resource->GetPMM()->AllocatePagesFromDescriptor(USER_PROCESS_STACK_SIZE / PAGE_SIZE);
    void* ProcessHeap  = Resource->GetPMM()->AllocatePagesFromDescriptor(USER_PROCESS_HEAP_SIZE / PAGE_SIZE);

    uint64_t ProcessHeapVirtualAddrStart = USER_PROCESS_VIRTUAL_BASE + CodeSize;
    uint64_t ProcessStackVirtualAddrStart = USER_PROCESS_VIRTUAL_STACK_TOP - USER_PROCESS_STACK_SIZE + 1;

    VirtualAddressSpace* AddressSpace = new VirtualAddressSpace(CodeAddr, CodeSize, USER_PROCESS_VIRTUAL_BASE, reinterpret_cast<uint64_t>(ProcessHeap), USER_PROCESS_HEAP_SIZE, ProcessHeapVirtualAddrStart,
                                                                reinterpret_cast<uint64_t>(ProcessStack), USER_PROCESS_STACK_SIZE, ProcessStackVirtualAddrStart);

    AddressSpace->Init(reinterpret_cast<uint64_t>(Resource->GetVMM()->CopyPageMapL4Table()), *Resource->GetPMM());

    uint64_t StackTop = USER_PROCESS_VIRTUAL_STACK_TOP;

    CpuState State = {};
    State.rip      = CodeAddr;
    State.rflags   = 0x202;                    // Bit1 always set, IF enabled
    State.rbp      = 0;                        // bottom of stack frame
    State.rsp      = (StackTop & ~0xFULL) - 8; // SysV entry alignment without a real CALL

    *reinterpret_cast<uint64_t*>(State.rsp) = 0;

    State.cs   = USER_CS;
    State.ss   = USER_SS;
    uint8_t Id = PM->CreateUserProcess(ProcessStack, State, AddressSpace);

    if (Id != 0xFF)
    {
        Sched->AddToReadyQueue(Id);
    }

    return Id;
}

void LogicLayer::KillProcess(uint8_t Id)
{
    void* StackToFree = PM->KillProcess(Id);
    if (StackToFree != nullptr)
    {
        Resource->kfree(StackToFree);
    }
    Sched->RemoveFromReadyQueue(Id);
}

bool LogicLayer::RunProcess(uint8_t Id)
{
    if (PM == nullptr || Resource == nullptr)
    {
        return false;
    }

    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status == PROCESS_TERMINATED)
    {
        return false;
    }

    Process* CurrentProcess = PM->GetRunningProcess();
    if (CurrentProcess == nullptr)
    {
        return false;
    }

    if (CurrentProcess == TargetProcess)
    {
        return true;
    }

    if (CurrentProcess->Status == PROCESS_RUNNING)
    {
        CurrentProcess->Status = PROCESS_READY;
    }
    TargetProcess->Status = PROCESS_RUNNING;
    PM->UpdateCurrentProcessId(TargetProcess->Id);
    Resource->TaskSwitch(&CurrentProcess->State, TargetProcess->State);

    if (TargetProcess->Status == PROCESS_RUNNING)
    {
        TargetProcess->Status = PROCESS_READY;
    }
    CurrentProcess->Status = PROCESS_RUNNING;

    return true;
}

void LogicLayer::SleepProcess(uint8_t Id, uint64_t WaitTicks)
{
    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status != PROCESS_RUNNING)
    {
        return;
    }

    TargetProcess->Status = PROCESS_BLOCKED;
    Sync->AddToSleepQueue(Id, WaitTicks);
    Sched->RemoveFromReadyQueue(Id);

    Ticks = 0; // Reset ticks to ensure the sleeping process gets the correct sleep duration
    Schedule();
}

void LogicLayer::WakeProcess(uint8_t Id)
{
    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status != PROCESS_BLOCKED)
    {
        return;
    }

    TargetProcess->Status = PROCESS_READY;
    Sync->RemoveFromSleepQueue(Id);
    Sched->AddToReadyQueue(Id);
}

void LogicLayer::BlockProcess(uint8_t Id)
{
    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status != PROCESS_RUNNING)
    {
        return;
    }

    TargetProcess->Status = PROCESS_BLOCKED;
    Sched->RemoveFromReadyQueue(Id);

    Ticks = 0; // Reset ticks
    Schedule();
}

void LogicLayer::UnblockProcess(uint8_t Id)
{
    Process* TargetProcess = PM->GetProcessById(Id);
    if (TargetProcess == nullptr || TargetProcess->Status != PROCESS_BLOCKED)
    {
        return;
    }

    TargetProcess->Status = PROCESS_READY;
    Sched->AddToReadyQueue(Id);
}

void LogicLayer::Tick()
{
    if (Sync != nullptr)
    {
        Sync->Tick();
        uint8_t IdToWake = Sync->GetNextProcessToWake();
        if (IdToWake != 0xFF)
        {
            WakeProcess(IdToWake);
        }
    }
}

void LogicLayer::Schedule()
{
    if (Sched == nullptr || PM == nullptr)
    {
        return;
    }

    uint8_t NextProcessId = Sched->SelectNextProcess();

    Resource->GetConsole()->printf_("Scheduling: Next process ID = %u\n", NextProcessId);

    if (NextProcessId == 0xFF)
    {
        return;
    }

    RunProcess(NextProcessId);
}

bool LogicLayer::isScheduling()
{
    return IsScheduling;
}

void LogicLayer::EnableScheduling()
{
    IsScheduling = true;
}

void LogicLayer::DisableScheduling()
{
    IsScheduling = false;
}