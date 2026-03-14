#include "TranslationLayer.hpp"

#include "Layers/Logic/LogicLayer.hpp"

TranslationLayer::TranslationLayer() : Logic(nullptr)
{
}

void TranslationLayer::Initialize(LogicLayer* Logic)
{
    this->Logic = Logic;
}

LogicLayer* TranslationLayer::GetLogicLayer() const
{
    return Logic;
}