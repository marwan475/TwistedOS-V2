#pragma once

#include <stddef.h>
#include <stdint.h>

// Used to load GDT and TSS.
typedef struct
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) DiscriptorRegister;

typedef struct
{
    union
    {
        uint64_t value;
        struct
        {
            uint64_t limit_15_0 : 16;
            uint64_t base_15_0 : 16;
            uint64_t base_23_16 : 8;
            uint64_t type : 4;
            uint64_t s : 1;
            uint64_t dpl : 2;
            uint64_t p : 1;
            uint64_t limit_19_16 : 4;
            uint64_t avl : 1;
            uint64_t l : 1;
            uint64_t d_b : 1;
            uint64_t g : 1;
            uint64_t base_31_24 : 8;
        };
    };
} X86_64Descriptor;

typedef struct
{
    X86_64Descriptor Descriptor;
    uint32_t         base_high;
    uint32_t         reserved;
} TSSDescriptor;

// Stores state of machine before task switch.
typedef struct
{
    uint32_t reserved_1;
    uint32_t RSP0_lower;
    uint32_t RSP0_upper;
    uint32_t RSP1_lower;
    uint32_t RSP1_upper;
    uint32_t RSP2_lower;
    uint32_t RSP2_upper;
    uint32_t reserved_2;
    uint32_t reserved_3;
    uint32_t IST1_lower;
    uint32_t IST1_upper;
    uint32_t IST2_lower;
    uint32_t IST2_upper;
    uint32_t IST3_lower;
    uint32_t IST3_upper;
    uint32_t IST4_lower;
    uint32_t IST4_upper;
    uint32_t IST5_lower;
    uint32_t IST5_upper;
    uint32_t IST6_lower;
    uint32_t IST6_upper;
    uint32_t IST7_lower;
    uint32_t IST7_upper;
    uint32_t reserved_4;
    uint32_t reserved_5;
    uint16_t reserved_6;
    uint16_t io_map_base;
} TSS;

typedef struct
{
    X86_64Descriptor null;
    X86_64Descriptor kernel_code_64;
    X86_64Descriptor kernel_data_64;
    X86_64Descriptor user_code_64;
    X86_64Descriptor user_data_64;
    TSSDescriptor    tss;
} GDT;

extern TSS                KernelTSS;
extern GDT                KernelGDT;
extern DiscriptorRegister KernelGDTR;

TSSDescriptor BuildTSSDescriptor(const TSS* tss);
GDT           BuildGDT(const TSSDescriptor& tss_descriptor);
void          InitGDT();
