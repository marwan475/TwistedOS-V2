#pragma once

#include <stdint.h>

class Dispatcher;

bool KernelSelfTestStart(Dispatcher* ActiveDispatcher);
void KernelSelfTestsOnSystemCall(uint64_t SystemCallNumber);
