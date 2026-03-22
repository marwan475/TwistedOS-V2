/**
 * File: KernelParameters.hpp
 * Author: Marwan Mostafa
 * Description: Shared kernel/boot parameters and related definitions.
 */

#pragma once

#include "../Bootloader/uefi.hpp"

static constexpr UINTN KERNEL_BASE_VIRTUAL_ADDR       = 0xFFFFFFFF80000000;
static constexpr UINTN KERNEL_HEAP_START              = 0xFFFFFFFF82000000;
static constexpr UINTN KERNEL_PAGE_SIZE               = 4096;
static constexpr UINTN KERNEL_HEAP_PAGES              = 8192;
static constexpr UINTN KERNEL_STACK_PAGES             = 128;
static constexpr UINTN KERNEL_STACK_SIZE              = (KERNEL_STACK_PAGES * KERNEL_PAGE_SIZE);
static constexpr UINTN KERNEL_PROCESS_STACK_SIZE      = KERNEL_PAGE_SIZE;
static constexpr UINTN USER_PROCESS_STACK_SIZE        = (2 * KERNEL_PAGE_SIZE);
static constexpr UINTN USER_PROCESS_HEAP_SIZE         = (2 * KERNEL_PAGE_SIZE);
static constexpr UINTN USER_PROCESS_VIRTUAL_BASE      = 0x400000;
static constexpr UINTN USER_PROCESS_VIRTUAL_STACK_TOP = 0x00007FFFFFFFFFFF;

typedef struct
{
    UINTN                  MemoryMapSize;
    EFI_MEMORY_DESCRIPTOR* MemoryMap;
    UINTN                  MapKey;
    UINTN                  DescriptorSize;
    UINT32                 DescriptorVersion;
} MemoryMapInfo;

typedef struct
{
    MemoryMapInfo                     MemoryMap;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE GopMode;
    UINTN                             InitramfsAddress;
    UINTN                             InitramfsSize;
    UINTN                             KernelEndVirtual;
    UINTN                             PageMapL4Table;
    UINTN                             NextPageAddress;
    UINTN                             CurrentDescriptor;
    UINTN                             RemainingPagesInDescriptor;
} KernelParameters;