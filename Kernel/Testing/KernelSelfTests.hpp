/**
 * File: KernelSelfTests.hpp
 * Author: Marwan Mostafa
 * Description: Kernel self-test declarations.
 */

#pragma once

#include <stdint.h>

class Dispatcher;

bool KernelSelfTestStart(Dispatcher* ActiveDispatcher);
void KernelSelfTestsOnSystemCall(uint64_t SystemCallNumber);
