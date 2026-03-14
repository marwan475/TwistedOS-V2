#include "LogicLayer.hpp"

#include "Layers/Resource/ResourceLayer.hpp"

LogicLayer::LogicLayer() : Resource(nullptr)
{
}

void LogicLayer::Initialize(ResourceLayer* Resource)
{
    this->Resource = Resource;
}

ResourceLayer* LogicLayer::GetResourceLayer() const
{
    return Resource;
}