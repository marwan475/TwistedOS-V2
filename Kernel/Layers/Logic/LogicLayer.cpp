#include "LogicLayer.hpp"

#include "Layers/Resource/ResourceLayer.hpp"

LogicLayer::LogicLayer() : Resource(nullptr), PM(nullptr)
{
}

LogicLayer::~LogicLayer()
{
	if (PM != nullptr)
	{
		delete PM;
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

uint8_t LogicLayer::CreateProcess(void (*EntryPoint)(), bool IsUserProcess){

	void* ProcessStack = Resource->kmalloc(PROCESS_STACK_SIZE);
	uint64_t StackTop = reinterpret_cast<uint64_t>(ProcessStack) + PROCESS_STACK_SIZE;

	CpuState State = {};
	State.rip    = reinterpret_cast<uint64_t>(EntryPoint);
	State.rflags = 0x202; // Bit1 always set, IF enabled
	State.rbp    = 0;     // bottom of stack frame
	State.rsp    = (StackTop & ~0xFULL) - 8; // SysV entry alignment without a real CALL
	State.cs     = IsUserProcess ? USER_CS : KERNEL_CS;
	State.ss     = IsUserProcess ? USER_SS : KERNEL_SS;

	*reinterpret_cast<uint64_t*>(State.rsp) = 0;

	uint8_t Id = PM->CreateProcess(ProcessStack, State);
	return Id;
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
	TargetProcess->Status = PROCESS_RUNNING;
	Resource->TaskSwitch(&CurrentProcess->State, TargetProcess->State);

	if (TargetProcess->Status == PROCESS_RUNNING)
	{
		TargetProcess->Status = PROCESS_READY;
	}
	CurrentProcess->Status = PROCESS_RUNNING;

	return true;
}