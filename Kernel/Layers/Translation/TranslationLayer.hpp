/**
 * File: TranslationLayer.hpp
 * Author: Marwan Mostafa
 * Description: Translation layer interface declarations between system layers.
 */

#pragma once

#include <stdint.h>

class LogicLayer;

class TranslationLayer
{
private:
    LogicLayer* Logic;

public:
    TranslationLayer();
    void Initialize(LogicLayer* Logic);
    int64_t HandlePosixSystemCallNumber(uint64_t SystemCallNumber, uint64_t Arg1, uint64_t Arg2, uint64_t Arg3, uint64_t Arg4, uint64_t Arg5, uint64_t Arg6);

    // Posix system call handlers
    int64_t HandleOpenSystemCall(const char* Path, uint64_t Flags);

    LogicLayer* GetLogicLayer() const;
};