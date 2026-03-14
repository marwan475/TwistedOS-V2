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

ProcessManager* LogicLayer::GetProcessManager() const
{
    return PM;
}