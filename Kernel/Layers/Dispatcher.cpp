/**
 * File: Dispatcher.cpp
 * Author: Marwan Mostafa
 * Description: Cross-layer dispatcher implementation.
 */

#include "Dispatcher.hpp"

#include <Arch/x86.hpp>
#include <Layers/Resource/Drivers/IDEController.hpp>
#include <Memory/KernelHeapAllocations.hpp>
#include <Testing/KernelSelfTests.hpp>

namespace
{
constexpr uint64_t TIMER_INTERRUPT_VECTOR       = 32;
constexpr uint64_t KEYBOARD_INTERRUPT_VECTOR    = 33;
constexpr uint64_t IDE_PRIMARY_INTERRUPT_VECTOR = 46;
constexpr uint64_t SYSCALL_INTERRUPT_VECTOR     = 128;
constexpr uint64_t SCHEDULER_TICK_INTERVAL      = 5;

const char* ExceptionName(uint64_t ExceptionVector)
{
    switch (ExceptionVector)
    {
        case 0:
            return "#DE Divide Error";
        case 1:
            return "#DB Debug";
        case 2:
            return "NMI Interrupt";
        case 3:
            return "#BP Breakpoint";
        case 4:
            return "#OF Overflow";
        case 5:
            return "#BR BOUND Range Exceeded";
        case 6:
            return "#UD Invalid Opcode";
        case 7:
            return "#NM Device Not Available";
        case 8:
            return "#DF Double Fault";
        case 9:
            return "Coprocessor Segment Overrun";
        case 10:
            return "#TS Invalid TSS";
        case 11:
            return "#NP Segment Not Present";
        case 12:
            return "#SS Stack-Segment Fault";
        case 13:
            return "#GP General Protection Fault";
        case 14:
            return "#PF Page Fault";
        case 15:
            return "Reserved";
        case 16:
            return "#MF x87 Floating-Point Exception";
        case 17:
            return "#AC Alignment Check";
        case 18:
            return "#MC Machine Check";
        case 19:
            return "#XM SIMD Floating-Point Exception";
        case 20:
            return "#VE Virtualization Exception";
        case 21:
            return "#CP Control Protection Exception";
        case 22:
            return "Reserved";
        case 23:
            return "Reserved";
        case 24:
            return "Reserved";
        case 25:
            return "Reserved";
        case 26:
            return "Reserved";
        case 27:
            return "Reserved";
        case 28:
            return "#HV Hypervisor Injection Exception";
        case 29:
            return "#VC VMM Communication Exception";
        case 30:
            return "#SX Security Exception";
        case 31:
            return "Reserved";
        default:
            return "Unknown Exception";
    }
}

const char* ProcessStateToString(ProcessState State)
{
    switch (State)
    {
        case PROCESS_RUNNING:
            return "running";
        case PROCESS_READY:
            return "ready";
        case PROCESS_BLOCKED:
            return "blocked";
        case PROCESS_TERMINATED:
            return "terminated";
        default:
            return "unknown";
    }
}

const char* ProcessLevelToString(ProcessLevel Level)
{
    switch (Level)
    {
        case PROCESS_LEVEL_KERNEL:
            return "kernel";
        case PROCESS_LEVEL_USER:
            return "user";
        default:
            return "unknown";
    }
}

const char* ProcessFileTypeToString(FILE_TYPE FileType)
{
    switch (FileType)
    {
        case FILE_TYPE_RAW_BINARY:
            return "raw";
        case FILE_TYPE_ELF:
            return "elf";
        default:
            return "unknown";
    }
}
} // namespace

Dispatcher* Dispatcher::ActiveDispatcher = nullptr;
uint64_t    Ticks                        = 0;

/**
 * Function: Dispatcher::Dispatcher
 * Description: Constructs a dispatcher instance.
 * Parameters:
 *   None
 * Returns:
 *   Dispatcher - Constructed dispatcher object.
 */
Dispatcher::Dispatcher()
{
}

/**
 * Function: Dispatcher::SetActive
 * Description: Sets the global active dispatcher instance.
 * Parameters:
 *   Dispatcher* dispatcher - Dispatcher instance to activate.
 * Returns:
 *   void - No return value.
 */
void Dispatcher::SetActive(Dispatcher* dispatcher)
{
    ActiveDispatcher = dispatcher;
}

/**
 * Function: Dispatcher::GetActive
 * Description: Returns the currently active dispatcher instance.
 * Parameters:
 *   None
 * Returns:
 *   Dispatcher* - Pointer to active dispatcher, or nullptr if not set.
 */
Dispatcher* Dispatcher::GetActive()
{
    return ActiveDispatcher;
}

/**
 * Function: Dispatcher::InitResourceLayer
 * Description: Initializes the resource layer and enables dispatcher-backed kernel allocation.
 * Parameters:
 *   const DispatcherParameters& Params - Startup parameters required for resource initialization.
 * Returns:
 *   void - No return value.
 */
void Dispatcher::InitResourceLayer(const DispatcherParameters& Params)
{
    Resource.Initialize(Params.PMM, Params.VMM, Params.Console, Params.KernelHeapVirtualAddrStart, Params.KernelHeapVirtualAddrEnd, Params.InitramfsAddress, Params.InitramfsSize);
    Resource.InitializeFrameBuffer(Params.GopMode);
    Resource.InitializeKernelHeapManager();
    KernelUseDispatcherAllocator();
    Resource.InitializeRamFileSystemManager();
    Resource.InitializeKeyboard();
    Resource.InitializeTTY();
    Resource.InitializeDeviceManager();
    Resource.InitPartitionManager();
}

/**
 * Function: Dispatcher::InitLogicLayer
 * Description: Initializes logic layer subsystems after resource layer setup.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void Dispatcher::InitLogicLayer()
{
    Logic.Initialize(&Resource);
    Logic.InitializeProcessManager();
    Logic.InitializeScheduler();
    Logic.InitializeSynchronizationManager();
    Logic.InitializeELFManager();
    Logic.InitializeVirtualFileSystem();
}

/**
 * Function: Dispatcher::InitTranslationLayer
 * Description: Initializes the translation layer and links it to the logic layer.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void Dispatcher::InitTranslationLayer()
{
    Translation.Initialize(&Logic);
}

/**
 * Function: Dispatcher::InitializeLayers
 * Description: Initializes resource, logic, and translation layers in startup order.
 * Parameters:
 *   const DispatcherParameters& Params - Startup parameters used by layer initializers.
 * Returns:
 *   void - No return value.
 */
void Dispatcher::InitializeLayers(const DispatcherParameters& Params)
{
    Params.Console->printf_("Initializing Resource Layer\n");
    InitResourceLayer(Params);
    Resource.GetTTY()->printf_("Resource Layer initialized\n");

    // Can use new operator post resource layer init
    Resource.GetTTY()->printf_("Initializing Logic Layer\n");
    InitLogicLayer();
    Resource.GetTTY()->printf_("Logic Layer initialized\n");

    Resource.GetTTY()->printf_("Initializing Translation Layer\n");
    InitTranslationLayer();
    Resource.GetTTY()->printf_("Translation Layer initialized\n");
}

/**
 * Function: Dispatcher::InterruptHandler
 * Description: Handles hardware and software interrupts and dispatches scheduling behavior.
 * Parameters:
 *   uint64_t InterruptNumber - Interrupt vector number.
 * Returns:
 *   void - No return value.
 */
void Dispatcher::InterruptHandler(uint64_t InterruptNumber)
{
    switch (InterruptNumber)
    {
        case TIMER_INTERRUPT_VECTOR:
        {
            Ticks++;
            if (Logic.isScheduling())
            {
                Logic.Tick();
                if (Ticks % SCHEDULER_TICK_INTERVAL == 0) // Schedule every 100 ticks (1 second if timer is set to 10ms)
                {
                    Ticks = 0;
                    Logic.Schedule();
                }
            }
        }
        break;
        case KEYBOARD_INTERRUPT_VECTOR:
        {
            Keyboard* ActiveKeyboard = Resource.GetKeyboard();
            if (ActiveKeyboard != nullptr)
            {
                ActiveKeyboard->HandleInterrupt();
            }
        }
        break;
        case IDE_PRIMARY_INTERRUPT_VECTOR:
        {
            DeviceManager* DeviceManagerInstance = Resource.GetDeviceManager();
            IDEController* DiskController        = (DeviceManagerInstance == nullptr) ? nullptr : DeviceManagerInstance->GetDiskController();
            if (DiskController == nullptr || !DiskController->HandleInterrupt())
            {
                Resource.GetTTY()->printf_("IDE interrupt with no active IDE driver\n");
            }
        }
        break;
        case SYSCALL_INTERRUPT_VECTOR:
        {
            Resource.GetTTY()->printf_("User syscall interrupt received (int 0x80)\n");
        }
        break;
        default:
            Resource.GetTTY()->printf_("Unhandled interrupt: %lu\n", InterruptNumber);
            while (1)
            {
                X86Halt();
            }

            break;
    }
}

/**
 * Function: Dispatcher::HandleException
 * Description: Handles CPU exceptions with verbose diagnostics and halts execution.
 * Parameters:
 *   const Registers* Regs - Captured interrupt register state.
 * Returns:
 *   void - No return value.
 */
void Dispatcher::HandleException(const Registers* Regs)
{
    TTY* Terminal = Resource.GetTTY();
    if (Terminal == nullptr)
    {
        while (1)
        {
            X86Halt();
        }
    }

    if (Regs == nullptr)
    {
        Terminal->printf_("CPU exception: <null register frame>\n");
        while (1)
        {
            X86Halt();
        }
    }

    uint64_t ExceptionVector = Regs->interrupt_number;

    switch (ExceptionVector)
    {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
        case 22:
        case 23:
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
        case 29:
        case 30:
        case 31:
            Terminal->Serialprintf("CPU exception: vec=%lu (%s) err=%p rip=%p cs=%p rflags=%p rsp=%p ss=%p\n", ExceptionVector, ExceptionName(ExceptionVector), (void*) Regs->error_code,
                                   (void*) Regs->rip, (void*) Regs->cs, (void*) Regs->rflags, (void*) Regs->rsp, (void*) Regs->ss);
            break;
        default:
            Terminal->Serialprintf("CPU exception: vec=%lu (out of architected range) err=%p rip=%p\n", ExceptionVector, (void*) Regs->error_code, (void*) Regs->rip);
            break;
    }

    if (ExceptionVector == 14)
    {
        uint64_t FaultAddress = 0;
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(FaultAddress));
        Terminal->Serialprintf("#PF detail: cr2=%p P=%u W/R=%u U/S=%u RSVD=%u I/D=%u\n", (void*) FaultAddress, (uint32_t) (Regs->error_code & 0x1ULL), (uint32_t) ((Regs->error_code >> 1) & 0x1ULL),
                               (uint32_t) ((Regs->error_code >> 2) & 0x1ULL), (uint32_t) ((Regs->error_code >> 3) & 0x1ULL), (uint32_t) ((Regs->error_code >> 4) & 0x1ULL));
    }

    Terminal->Serialprintf("Exception regs: rax=%p rbx=%p rcx=%p rdx=%p rbp=%p rsi=%p rdi=%p\n", (void*) Regs->rax, (void*) Regs->rbx, (void*) Regs->rcx, (void*) Regs->rdx, (void*) Regs->rbp,
                           (void*) Regs->rsi, (void*) Regs->rdi);
    Terminal->Serialprintf("Exception regs: r8=%p r9=%p r10=%p r11=%p r12=%p r13=%p r14=%p r15=%p\n", (void*) Regs->r8, (void*) Regs->r9, (void*) Regs->r10, (void*) Regs->r11, (void*) Regs->r12,
                           (void*) Regs->r13, (void*) Regs->r14, (void*) Regs->r15);

    ProcessManager* PM             = Logic.GetProcessManager();
    Process*        CurrentProcess = (PM == nullptr) ? nullptr : PM->GetCurrentProcess();
    if (CurrentProcess == nullptr)
    {
        Terminal->Serialprintf("Exception process: <none>\n");
    }
    else
    {
        uint64_t ProcessPageTable = 0;
        uint64_t CodeStart        = 0;
        uint64_t CodeSize         = 0;
        uint64_t HeapStart        = 0;
        uint64_t HeapSize         = 0;
        uint64_t StackStart       = 0;
        uint64_t StackSize        = 0;
        if (CurrentProcess->AddressSpace != nullptr)
        {
            ProcessPageTable = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
            CodeStart        = CurrentProcess->AddressSpace->GetCodeVirtualAddressStart();
            CodeSize         = CurrentProcess->AddressSpace->GetCodeSize();
            HeapStart        = CurrentProcess->AddressSpace->GetHeapVirtualAddressStart();
            HeapSize         = CurrentProcess->AddressSpace->GetHeapSize();
            StackStart       = CurrentProcess->AddressSpace->GetStackVirtualAddressStart();
            StackSize        = CurrentProcess->AddressSpace->GetStackSize();
        }

        uint64_t LiveFSBase      = GetUserFSBase();
        uint64_t ActivePageTable = Resource.ReadCurrentPageTable();

        Terminal->Serialprintf("Exception process: id=%u parent=%u state=%s level=%s type=%s waiting_sysret=%u saved_syscall=%u addrspace=%p cr3=%p proc_cr3=%p\n", CurrentProcess->Id,
                               CurrentProcess->ParrentId, ProcessStateToString(CurrentProcess->Status), ProcessLevelToString(CurrentProcess->Level), ProcessFileTypeToString(CurrentProcess->FileType),
                               CurrentProcess->WaitingForSystemCallReturn ? 1U : 0U, CurrentProcess->HasSavedSystemCallFrame ? 1U : 0U, CurrentProcess->AddressSpace, (void*) ActivePageTable,
                               (void*) ProcessPageTable);
        Terminal->Serialprintf("Exception process FS: live=%p saved=%p\n", (void*) LiveFSBase, (void*) CurrentProcess->UserFSBase);
        Terminal->Serialprintf("Exception process ranges: code=[%p..%p) heap=[%p..%p) stack=[%p..%p)\n", (void*) CodeStart, (void*) (CodeStart + CodeSize), (void*) HeapStart,
                               (void*) (HeapStart + HeapSize), (void*) StackStart, (void*) (StackStart + StackSize));
    }

    while (1)
    {
        X86Halt();
    }
}

/**
 * Function: Dispatcher::HandleSystemCall
 * Description: Handles syscall dispatch hook and runs kernel self-tests for syscall numbers.
 * Parameters:
 *   uint64_t SystemCallNumber - System call identifier.
 *   uint64_t Arg1 - First syscall argument.
 *   uint64_t Arg2 - Second syscall argument.
 *   uint64_t Arg3 - Third syscall argument.
 *   uint64_t Arg4 - Fourth syscall argument.
 *   uint64_t Arg5 - Fifth syscall argument.
 *   uint64_t Arg6 - Sixth syscall argument.
 * Returns:
 *   int64_t - Syscall return value propagated to userspace.
 */
int64_t Dispatcher::HandleSystemCall(uint64_t SystemCallNumber, uint64_t Arg1, uint64_t Arg2, uint64_t Arg3, uint64_t Arg4, uint64_t Arg5, uint64_t Arg6)
{
    KernelSelfTestsOnSystemCall(SystemCallNumber);

#ifdef DEBUG_BUILD
    TTY* Terminal = Resource.GetTTY();
    if (Terminal != nullptr)
    {
        Terminal->Serialprintf("syscall: n=%lu a1=%p a2=%p a3=%p a4=%p a5=%p a6=%p\n", SystemCallNumber, (void*) Arg1, (void*) Arg2, (void*) Arg3, (void*) Arg4, (void*) Arg5, (void*) Arg6);
    }
#endif

    int64_t Result = Translation.HandlePosixSystemCallNumber(SystemCallNumber, Arg1, Arg2, Arg3, Arg4, Arg5, Arg6);

#ifdef DEBUG_BUILD
    if (Terminal != nullptr)
    {
        Terminal->Serialprintf("syscall_ret: n=%lu r=%lld\n", SystemCallNumber, static_cast<long long>(Result));
    }
#endif

    return Result;
}

/**
 * Function: Dispatcher::GetResourceLayer
 * Description: Returns mutable access to the resource layer.
 * Parameters:
 *   None
 * Returns:
 *   ResourceLayer* - Pointer to the resource layer.
 */
ResourceLayer* Dispatcher::GetResourceLayer()
{
    return &Resource;
}

/**
 * Function: Dispatcher::GetLogicLayer
 * Description: Returns mutable access to the logic layer.
 * Parameters:
 *   None
 * Returns:
 *   LogicLayer* - Pointer to the logic layer.
 */
LogicLayer* Dispatcher::GetLogicLayer()
{
    return &Logic;
}

/**
 * Function: Dispatcher::GetTranslationLayer
 * Description: Returns mutable access to the translation layer.
 * Parameters:
 *   None
 * Returns:
 *   TranslationLayer* - Pointer to the translation layer.
 */
TranslationLayer* Dispatcher::GetTranslationLayer()
{
    return &Translation;
}

/**
 * Function: Dispatcher::GetResourceLayer (const)
 * Description: Returns read-only access to the resource layer.
 * Parameters:
 *   None
 * Returns:
 *   const ResourceLayer* - Const pointer to the resource layer.
 */
const ResourceLayer* Dispatcher::GetResourceLayer() const
{
    return &Resource;
}

/**
 * Function: Dispatcher::GetLogicLayer (const)
 * Description: Returns read-only access to the logic layer.
 * Parameters:
 *   None
 * Returns:
 *   const LogicLayer* - Const pointer to the logic layer.
 */
const LogicLayer* Dispatcher::GetLogicLayer() const
{
    return &Logic;
}

/**
 * Function: Dispatcher::GetTranslationLayer (const)
 * Description: Returns read-only access to the translation layer.
 * Parameters:
 *   None
 * Returns:
 *   const TranslationLayer* - Const pointer to the translation layer.
 */
const TranslationLayer* Dispatcher::GetTranslationLayer() const
{
    return &Translation;
}
