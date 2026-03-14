#include "Dispatcher.hpp"

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
}

void Dispatcher::InitLogicLayer()
{
    Logic.Initialize(&Resource);
}

void Dispatcher::InitTranslationLayer()
{
    Translation.Initialize(&Logic);
}

void Dispatcher::InitializeLayers(const DispatcherParameters& Params)
{
    InitResourceLayer(Params);
    InitLogicLayer();
    InitTranslationLayer();
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
