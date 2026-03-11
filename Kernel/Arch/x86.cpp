#include <Arch/x86.hpp>

TSS                KernelTSS  = {};
GDT                KernelGDT  = {};
DiscriptorRegister KernelGDTR = {};

TSSDescriptor BuildTSSDescriptor(const TSS* tss)
{
    uint64_t tss_address = (uint64_t) tss;
    uint32_t tss_limit   = (uint32_t) (sizeof(TSS) - 1);

    TSSDescriptor tss_descriptor    = {};
    tss_descriptor.Descriptor.value = 0;

    tss_descriptor.Descriptor.value |= (uint64_t) (tss_limit & 0xFFFF);               // limit[15:0]
    tss_descriptor.Descriptor.value |= (uint64_t) (tss_address & 0xFFFFFF) << 16;     // base[23:0]
    tss_descriptor.Descriptor.value |= (uint64_t) 0x9 << 40;                          // type = available 64-bit TSS
    tss_descriptor.Descriptor.value |= (uint64_t) 1 << 47;                            // present
    tss_descriptor.Descriptor.value |= (uint64_t) ((tss_limit >> 16) & 0xF) << 48;    // limit[19:16]
    tss_descriptor.Descriptor.value |= (uint64_t) ((tss_address >> 24) & 0xFF) << 56; // base[31:24]

    tss_descriptor.base_high = (uint32_t) ((tss_address >> 32) & 0xFFFFFFFF);
    tss_descriptor.reserved  = 0;

    return tss_descriptor;
}

GDT BuildGDT(const TSSDescriptor& tss_descriptor)
{
    GDT gdt = {};

    gdt.null.value           = 0x0000000000000000;
    gdt.kernel_code_64.value = 0x00AF9A000000FFFF;
    gdt.kernel_data_64.value = 0x00CF92000000FFFF;
    gdt.user_code_64.value   = 0x00AFFA000000FFFF;
    gdt.user_data_64.value   = 0x00CFF2000000FFFF;
    gdt.tss                  = tss_descriptor;

    return gdt;
}

void InitGDT()
{
    KernelTSS.io_map_base = sizeof(TSS); // No IO bitmap, so set base to end of TSS

    TSSDescriptor tss_descriptor = BuildTSSDescriptor(&KernelTSS);
    KernelGDT                    = BuildGDT(tss_descriptor);

    KernelGDTR.limit = sizeof(GDT) - 1;
    KernelGDTR.base  = (uint64_t) &KernelGDT;

    __asm__ __volatile__("cli\n"
                         "lgdt %[gdt]\n"
                         "ltr %[tss]\n"

                         // Reload CS via far return.
                         "pushq $0x8\n"
                         "leaq 1f(%%rip), %%rax\n"
                         "pushq %%rax\n"
                         "lretq\n"

                         "1:\n"
                         // Reload data and stack segments.
                         "movw $0x10, %%ax\n"
                         "movw %%ax, %%ds\n"
                         "movw %%ax, %%es\n"
                         "movw %%ax, %%ss\n"
                         // Reload FS/GS selectors.
                         "movw %%ax, %%fs\n"
                         "movw %%ax, %%gs\n"
                         :
                         : [gdt] "m"(KernelGDTR), [tss] "r"((uint16_t) offsetof(GDT, tss))
                         : "rax", "memory");
}

static_assert(sizeof(DiscriptorRegister) == 10, "GDTR must be 10 bytes in long mode");
static_assert(sizeof(TSSDescriptor) == 16, "TSS descriptor must be 16 bytes");
static_assert(offsetof(GDT, tss) == 0x28, "TSS selector offset must be 0x28");
