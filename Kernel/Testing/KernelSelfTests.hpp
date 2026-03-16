#pragma once

#include <stdint.h>

class Dispatcher;

bool KernelMultiTaskingTest(Dispatcher* ActiveDispatcher);
void KernelSelfTestsOnSystemCall(uint64_t SystemCallNumber);
