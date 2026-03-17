/**
 * File: Dispatcher.cpp
 * Author: Marwan Mostafa
 * Description: Cross-layer dispatcher implementation.
 */

#include "Dispatcher.hpp"

#include <Memory/KernelHeapAllocations.hpp>
#include <Testing/KernelSelfTests.hpp>

namespace
{
constexpr uint64_t TIMER_INTERRUPT_VECTOR   = 32;
constexpr uint64_t SYSCALL_INTERRUPT_VECTOR = 128;
constexpr uint64_t SCHEDULER_TICK_INTERVAL  = 100;
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
    Resource.InitializeKernelHeapManager();
    Resource.InitializeRamFileSystemManager();
    KernelUseDispatcherAllocator();
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
    Resource.GetConsole()->printf_("Resource Layer initialized\n");

    // Can use new operator post resource layer init
    Resource.GetConsole()->printf_("Initializing Logic Layer\n");
    InitLogicLayer();
    Resource.GetConsole()->printf_("Logic Layer initialized\n");

    Resource.GetConsole()->printf_("Initializing Translation Layer\n");
    InitTranslationLayer();
    Resource.GetConsole()->printf_("Translation Layer initialized\n");
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
        case SYSCALL_INTERRUPT_VECTOR:
        {
            Resource.GetConsole()->printf_("User syscall interrupt received (int 0x80)\n");
        }
        break;
        default:
            Resource.GetConsole()->printf_("Unhandled interrupt: %lu\n", InterruptNumber);
            while (1)
            {
                __asm__ __volatile__("hlt");
            }

            break;
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
 *   void - No return value.
 */
void Dispatcher::HandleSystemCall(uint64_t SystemCallNumber, uint64_t Arg1, uint64_t Arg2, uint64_t Arg3, uint64_t Arg4, uint64_t Arg5, uint64_t Arg6)
{
    (void) Arg1;
    (void) Arg2;
    (void) Arg3;
    (void) Arg4;
    (void) Arg5;
    (void) Arg6;

    KernelSelfTestsOnSystemCall(SystemCallNumber);
    Resource.GetConsole()->printf_("User syscall instruction received (syscall=%lu, a1=%lu, a2=%lu, a3=%lu, a4=%lu, a5=%lu, a6=%lu)\n", SystemCallNumber, Arg1, Arg2, Arg3, Arg4, Arg5, Arg6);
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
