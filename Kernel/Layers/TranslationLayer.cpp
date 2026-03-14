#include "TranslationLayer.hpp"

#include "LogicLayer.hpp"

TranslationLayer::TranslationLayer()
	: Logic(nullptr)
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