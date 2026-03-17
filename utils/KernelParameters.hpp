/**
 * File: KernelParameters.hpp
 * Author: Marwan Mostafa
 * Description: Shared kernel/boot parameters and related definitions.
 */

#pragma once

#include "../Bootloader/uefi.hpp"

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