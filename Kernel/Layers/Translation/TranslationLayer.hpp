/**
 * File: TranslationLayer.hpp
 * Author: Marwan Mostafa
 * Description: Translation layer interface declarations between system layers.
 */

#pragma once

class LogicLayer;

class TranslationLayer
{
private:
    LogicLayer* Logic;

public:
    TranslationLayer();
    void Initialize(LogicLayer* Logic);

    LogicLayer* GetLogicLayer() const;
};