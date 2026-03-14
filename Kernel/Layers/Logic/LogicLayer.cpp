#include "LogicLayer.hpp"

#include "Layers/Resource/ResourceLayer.hpp"

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

uint8_t LogicLayer::CreateProcess(void (*EntryPoint)())
{
    void*    ProcessStack = Resource->kmalloc(PROCESS_STACK_SIZE);
    uint64_t StackTop     = reinterpret_cast<uint64_t>(ProcessStack) + PROCESS_STACK_SIZE;

    CpuState State = {};
    State.rip      = reinterpret_cast<uint64_t>(EntryPoint);
    State.rflags   = 0x202;                    // Bit1 always set, IF enabled
    State.rbp      = 0;                        // bottom of stack frame
    State.rsp      = (StackTop & ~0xFULL) - 8; // SysV entry alignment without a real CALL
    State.cs       = KERNEL_CS;
    State.ss       = KERNEL_SS;

    *reinterpret_cast<uint64_t*>(State.rsp) = 0;

    uint8_t Id = PM->CreateProcess(ProcessStack, State);
    Sched->AddToReadyQueue(Id);

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

    CurrentProcess->Status = PROCESS_READY;
    TargetProcess->Status  = PROCESS_RUNNING;
    Resource->TaskSwitch(&CurrentProcess->State, TargetProcess->State);

    if (TargetProcess->Status == PROCESS_RUNNING)
    {
        TargetProcess->Status = PROCESS_READY;
    }
    CurrentProcess->Status = PROCESS_RUNNING;

    return true;
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