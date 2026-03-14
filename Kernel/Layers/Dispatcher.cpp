#include "Dispatcher.hpp"

#include <Memory/KernelHeapAllocations.hpp>

Dispatcher* Dispatcher::ActiveDispatcher = nullptr;

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
    Resource.Initialize(Params.PMM, Params.VMM, Params.Console, Params.KernelHeapVirtualAddrStart,
                        Params.KernelHeapVirtualAddrEnd);
    Resource.InitializeKernelHeapManager();
    KernelUseDispatcherAllocator();
}

void Dispatcher::InitLogicLayer()
{
    Logic.Initialize(&Resource);
    Logic.InitializeProcessManager();
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
