#pragma once

class ResourceLayer;

class LogicLayer
{
private:
    ResourceLayer* Resource;

public:
    LogicLayer();
    void Initialize(ResourceLayer* Resource);

    ResourceLayer* GetResourceLayer() const;
};