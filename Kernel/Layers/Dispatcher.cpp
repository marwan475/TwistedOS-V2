#include "Dispatcher.hpp"

#include <Memory/KernelHeapAllocations.hpp>
#include <Testing/KernelSelfTests.hpp>

Dispatcher* Dispatcher::ActiveDispatcher = nullptr;
uint64_t    Ticks                        = 0;

Dispatcher::Dispatcher()
{
}

void Dispatcher::SetActive(Dispatcher* dispatcher)
{
    ActiveDispatcher = dispatcher;
}

Dispatcher* Dispatcher::GetActive()
{
    return ActiveDispatcher;
}

void Dispatcher::InitResourceLayer(const DispatcherParameters& Params)
{
    Resource.Initialize(Params.PMM, Params.VMM, Params.Console, Params.KernelHeapVirtualAddrStart, Params.KernelHeapVirtualAddrEnd, Params.InitramfsAddress, Params.InitramfsSize);
    Resource.InitializeKernelHeapManager();
    Resource.InitializeRamFileSystemManager();
    KernelUseDispatcherAllocator();
}

void Dispatcher::InitLogicLayer()
{
    Logic.Initialize(&Resource);
    Logic.InitializeProcessManager();
    Logic.InitializeScheduler();
    Logic.InitializeSynchronizationManager();
}

void Dispatcher::InitTranslationLayer()
{
    Translation.Initialize(&Logic);
}

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

void Dispatcher::InterruptHandler(uint64_t InterruptNumber)
{
    switch (InterruptNumber)
    {
        case 32:
        {
            Ticks++;
            if (Logic.isScheduling())
            {
                Logic.Tick();
                if (Ticks % 100 == 0) // Schedule every 100 ticks (1 second if timer is set to 10ms)
                {
                    Ticks = 0;
                    Logic.Schedule();
                }
            }
        }
        break;
        case 128:
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

void Dispatcher::HandleSystemCall(uint64_t SystemCallNumber, uint64_t Arg1, uint64_t Arg2, uint64_t Arg3, uint64_t Arg4, uint64_t Arg5, uint64_t Arg6)
{
    KernelSelfTestsOnSystemCall(SystemCallNumber);
    Resource.GetConsole()->printf_("User syscall instruction received (syscall=%lu, a1=%lu, a2=%lu, a3=%lu, a4=%lu, a5=%lu, a6=%lu)\n", SystemCallNumber, Arg1, Arg2, Arg3, Arg4, Arg5, Arg6);
}

ResourceLayer* Dispatcher::GetResourceLayer()
{
    return &Resource;
}

LogicLayer* Dispatcher::GetLogicLayer()
{
    return &Logic;
}

TranslationLayer* Dispatcher::GetTranslationLayer()
{
    return &Translation;
}

const ResourceLayer* Dispatcher::GetResourceLayer() const
{
    return &Resource;
}

const LogicLayer* Dispatcher::GetLogicLayer() const
{
    return &Logic;
}

const TranslationLayer* Dispatcher::GetTranslationLayer() const
{
    return &Translation;
}
