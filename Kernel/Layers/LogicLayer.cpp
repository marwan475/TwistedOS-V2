#include "LogicLayer.hpp"

#include "ResourceLayer.hpp"

LogicLayer::LogicLayer()
	: Resource(nullptr)
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