#pragma once

#include <stddef.h>

using KernelAllocFn = void* (*) (size_t);
using KernelFreeFn  = void (*)(void*);

void KernelSetAllocator(KernelAllocFn AllocFn, KernelFreeFn FreeFn);
void KernelUseDispatcherAllocator();