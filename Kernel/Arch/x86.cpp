#include <Arch/x86.hpp>

#include <Logging/FrameBufferConsole.hpp>

TSS                KernelTSS  = {};
GDT                KernelGDT  = {};
DiscriptorRegister KernelGDTR = {};

static constexpr uint16_t GDT_CODE_SEGMENT = 0x08;

static IDTentry IDT[256] = {};
static IDTdescription IDTDescriptor = {sizeof(IDT) - 1, IDT};

static inline void LoadIDT(const IDTdescription* idt_descriptor)
{
    __asm__ __volatile__("lidt %0" : : "m"(*idt_descriptor) : "memory");
}

static void SetIDTEntry(int interrupt, void (*base)(), uint16_t segment, uint8_t flags)
{
    uintptr_t base_addr        = reinterpret_cast<uintptr_t>(base);
    IDT[interrupt].offset_low  = (uint16_t) (base_addr & 0xFFFF);
    IDT[interrupt].selector    = segment;
    IDT[interrupt].ist         = 0;
    IDT[interrupt].type_attr   = flags;
    IDT[interrupt].offset_mid  = (uint16_t) ((base_addr >> 16) & 0xFFFF);
    IDT[interrupt].offset_high = (uint32_t) ((base_addr >> 32) & 0xFFFFFFFF);
    IDT[interrupt].zero        = 0;
}

static void EnableIDTEntry(int interrupt)
{
    IDT[interrupt].type_attr |= IDT_FLAG_PRESENT;
}

static void DisableIDTEntry(int interrupt)
{
    IDT[interrupt].type_attr &= ~IDT_FLAG_PRESENT;
}

static void (*const ISRHandlers[256])() = {
    ISR0,   ISR1,   ISR2,   ISR3,   ISR4,   ISR5,   ISR6,   ISR7,   ISR8,   ISR9,   ISR10,  ISR11,  ISR12,
    ISR13,  ISR14,  ISR15,  ISR16,  ISR17,  ISR18,  ISR19,  ISR20,  ISR21,  ISR22,  ISR23,  ISR24,  ISR25,
    ISR26,  ISR27,  ISR28,  ISR29,  ISR30,  ISR31,  ISR32,  ISR33,  ISR34,  ISR35,  ISR36,  ISR37,  ISR38,
    ISR39,  ISR40,  ISR41,  ISR42,  ISR43,  ISR44,  ISR45,  ISR46,  ISR47,  ISR48,  ISR49,  ISR50,  ISR51,
    ISR52,  ISR53,  ISR54,  ISR55,  ISR56,  ISR57,  ISR58,  ISR59,  ISR60,  ISR61,  ISR62,  ISR63,  ISR64,
    ISR65,  ISR66,  ISR67,  ISR68,  ISR69,  ISR70,  ISR71,  ISR72,  ISR73,  ISR74,  ISR75,  ISR76,  ISR77,
    ISR78,  ISR79,  ISR80,  ISR81,  ISR82,  ISR83,  ISR84,  ISR85,  ISR86,  ISR87,  ISR88,  ISR89,  ISR90,
    ISR91,  ISR92,  ISR93,  ISR94,  ISR95,  ISR96,  ISR97,  ISR98,  ISR99,  ISR100, ISR101, ISR102, ISR103,
    ISR104, ISR105, ISR106, ISR107, ISR108, ISR109, ISR110, ISR111, ISR112, ISR113, ISR114, ISR115, ISR116,
    ISR117, ISR118, ISR119, ISR120, ISR121, ISR122, ISR123, ISR124, ISR125, ISR126, ISR127, ISR128, ISR129,
    ISR130, ISR131, ISR132, ISR133, ISR134, ISR135, ISR136, ISR137, ISR138, ISR139, ISR140, ISR141, ISR142,
    ISR143, ISR144, ISR145, ISR146, ISR147, ISR148, ISR149, ISR150, ISR151, ISR152, ISR153, ISR154, ISR155,
    ISR156, ISR157, ISR158, ISR159, ISR160, ISR161, ISR162, ISR163, ISR164, ISR165, ISR166, ISR167, ISR168,
    ISR169, ISR170, ISR171, ISR172, ISR173, ISR174, ISR175, ISR176, ISR177, ISR178, ISR179, ISR180, ISR181,
    ISR182, ISR183, ISR184, ISR185, ISR186, ISR187, ISR188, ISR189, ISR190, ISR191, ISR192, ISR193, ISR194,
    ISR195, ISR196, ISR197, ISR198, ISR199, ISR200, ISR201, ISR202, ISR203, ISR204, ISR205, ISR206, ISR207,
    ISR208, ISR209, ISR210, ISR211, ISR212, ISR213, ISR214, ISR215, ISR216, ISR217, ISR218, ISR219, ISR220,
    ISR221, ISR222, ISR223, ISR224, ISR225, ISR226, ISR227, ISR228, ISR229, ISR230, ISR231, ISR232, ISR233,
    ISR234, ISR235, ISR236, ISR237, ISR238, ISR239, ISR240, ISR241, ISR242, ISR243, ISR244, ISR245, ISR246,
    ISR247, ISR248, ISR249, ISR250, ISR251, ISR252, ISR253, ISR254, ISR255,
};

static void ISR_init()
{
    //DisableIDTEntry(0);

    for (int interrupt = 0; interrupt < 256; interrupt++)
    {
        SetIDTEntry(interrupt, ISRHandlers[interrupt], GDT_CODE_SEGMENT, 0x8E);
        EnableIDTEntry(interrupt);
    }
}


extern "C" void ISRHANDLER(Registers* reg)
{
    FrameBufferConsole* Console = FrameBufferConsole::GetActive();

    if (Console != nullptr)
    {
        Console->printf_("Interrupt %lu\n", reg->interrupt_number);
    }

    if (reg->interrupt_number < 32)
    {
        // Panic
    }
    else
    {
        if (reg->interrupt_number >= 32 && reg->interrupt_number < 42)
        {
            // sending response to interrupt to PIC
        }

        if (reg->interrupt_number >= 40)
        {
            // write8bitportSlow(picSinput, 0x20);
        }
    }

    while(true){
        __asm__ __volatile__("hlt");
    }
}

void InitInterrupts()
{
    ISR_init();
    LoadIDT(&IDTDescriptor);
}

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
