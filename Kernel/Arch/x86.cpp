#include <Arch/x86.hpp>
#include <Layers/Dispatcher.hpp>
#include <Logging/FrameBufferConsole.hpp>

#define IA32_EFER_MSR 0xC0000080
#define IA32_STAR_MSR 0xC0000081
#define IA32_LSTAR_MSR 0xC0000082
#define IA32_FMASK_MSR 0xC0000084

#define IA32_EFER_SCE (1ULL << 0)
#define RFLAGS_IF (1ULL << 9)

#define SYSCALL_STAR_USER_CS_SHIFT 48
#define SYSCALL_STAR_KERNEL_CS_SHIFT 32

TSS                KernelTSS  = {};
GDT                KernelGDT  = {};
DiscriptorRegister KernelGDTR = {};

static IDTentry       IDT[256]                                                 = {};
static IDTdescription IDTDescriptor                                            = {sizeof(IDT) - 1, IDT};
static uint8_t        KernelInterruptStack[16384] __attribute__((aligned(16))) = {};

static inline void LoadIDT(const IDTdescription* idt_descriptor)
{
    __asm__ __volatile__("lidt %0" : : "m"(*idt_descriptor) : "memory");
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value = 0;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void WriteMSR(uint32_t msr, uint64_t value)
{
    uint32_t low  = (uint32_t) (value & 0xFFFFFFFF);
    uint32_t high = (uint32_t) ((value >> 32) & 0xFFFFFFFF);
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t ReadMSR(uint32_t msr)
{
    uint32_t low  = 0;
    uint32_t high = 0;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t) high << 32) | low;
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
        ISR0,   ISR1,   ISR2,   ISR3,   ISR4,   ISR5,   ISR6,   ISR7,   ISR8,   ISR9,   ISR10,  ISR11,  ISR12,  ISR13,  ISR14,  ISR15,  ISR16,  ISR17,  ISR18,  ISR19,  ISR20,  ISR21,  ISR22,  ISR23,
        ISR24,  ISR25,  ISR26,  ISR27,  ISR28,  ISR29,  ISR30,  ISR31,  ISR32,  ISR33,  ISR34,  ISR35,  ISR36,  ISR37,  ISR38,  ISR39,  ISR40,  ISR41,  ISR42,  ISR43,  ISR44,  ISR45,  ISR46,  ISR47,
        ISR48,  ISR49,  ISR50,  ISR51,  ISR52,  ISR53,  ISR54,  ISR55,  ISR56,  ISR57,  ISR58,  ISR59,  ISR60,  ISR61,  ISR62,  ISR63,  ISR64,  ISR65,  ISR66,  ISR67,  ISR68,  ISR69,  ISR70,  ISR71,
        ISR72,  ISR73,  ISR74,  ISR75,  ISR76,  ISR77,  ISR78,  ISR79,  ISR80,  ISR81,  ISR82,  ISR83,  ISR84,  ISR85,  ISR86,  ISR87,  ISR88,  ISR89,  ISR90,  ISR91,  ISR92,  ISR93,  ISR94,  ISR95,
        ISR96,  ISR97,  ISR98,  ISR99,  ISR100, ISR101, ISR102, ISR103, ISR104, ISR105, ISR106, ISR107, ISR108, ISR109, ISR110, ISR111, ISR112, ISR113, ISR114, ISR115, ISR116, ISR117, ISR118, ISR119,
        ISR120, ISR121, ISR122, ISR123, ISR124, ISR125, ISR126, ISR127, ISR128, ISR129, ISR130, ISR131, ISR132, ISR133, ISR134, ISR135, ISR136, ISR137, ISR138, ISR139, ISR140, ISR141, ISR142, ISR143,
        ISR144, ISR145, ISR146, ISR147, ISR148, ISR149, ISR150, ISR151, ISR152, ISR153, ISR154, ISR155, ISR156, ISR157, ISR158, ISR159, ISR160, ISR161, ISR162, ISR163, ISR164, ISR165, ISR166, ISR167,
        ISR168, ISR169, ISR170, ISR171, ISR172, ISR173, ISR174, ISR175, ISR176, ISR177, ISR178, ISR179, ISR180, ISR181, ISR182, ISR183, ISR184, ISR185, ISR186, ISR187, ISR188, ISR189, ISR190, ISR191,
        ISR192, ISR193, ISR194, ISR195, ISR196, ISR197, ISR198, ISR199, ISR200, ISR201, ISR202, ISR203, ISR204, ISR205, ISR206, ISR207, ISR208, ISR209, ISR210, ISR211, ISR212, ISR213, ISR214, ISR215,
        ISR216, ISR217, ISR218, ISR219, ISR220, ISR221, ISR222, ISR223, ISR224, ISR225, ISR226, ISR227, ISR228, ISR229, ISR230, ISR231, ISR232, ISR233, ISR234, ISR235, ISR236, ISR237, ISR238, ISR239,
        ISR240, ISR241, ISR242, ISR243, ISR244, ISR245, ISR246, ISR247, ISR248, ISR249, ISR250, ISR251, ISR252, ISR253, ISR254, ISR255,
};

static void ISR_init()
{
    DisableIDTEntry(0); // Avoid Compiler Warning

    for (int interrupt = 0; interrupt < 256; interrupt++)
    {
        SetIDTEntry(interrupt, ISRHandlers[interrupt], GDT_CODE_SEGMENT, IDT_DEFAULT_GATE_FLAGS);
        EnableIDTEntry(interrupt);
    }

    SetIDTEntry(0x80, ISRHandlers[0x80], GDT_CODE_SEGMENT, IDT_FLAG_PRESENT | IDT_FLAG_RING3 | IDT_FLAG_GATE_32BIT_INT);
}

void InitTimer()
{
    constexpr uint32_t PIT_BASE_FREQUENCY_HZ = 1193182;
    constexpr uint32_t TIMER_INTERVAL_MS     = 10;
    constexpr uint32_t TIMER_FREQUENCY_HZ    = 1000 / TIMER_INTERVAL_MS;
    constexpr uint16_t PIT_DIVISOR           = (uint16_t) (PIT_BASE_FREQUENCY_HZ / TIMER_FREQUENCY_HZ);

    outb(PIT_COMMAND_PORT, PIT_CHANNEL0_SQUARE_WAVE);
    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t) (PIT_DIVISOR & 0xFF));
    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t) ((PIT_DIVISOR >> 8) & 0xFF));
}

void RemapPIC()
{
    outb(PIC1_COMMAND_PORT, PIC_INIT_COMMAND);
    outb(PIC2_COMMAND_PORT, PIC_INIT_COMMAND);

    outb(PIC1_DATA_PORT, PIC1_VECTOR_OFFSET);
    outb(PIC2_DATA_PORT, PIC2_VECTOR_OFFSET);

    outb(PIC1_DATA_PORT, PIC1_CASCADE_IDENTITY);
    outb(PIC2_DATA_PORT, PIC2_CASCADE_IDENTITY);

    outb(PIC1_DATA_PORT, PIC_8086_MODE);
    outb(PIC2_DATA_PORT, PIC_8086_MODE);

    outb(PIC1_DATA_PORT, PIC_RESTORE_MASK_NONE);
    outb(PIC2_DATA_PORT, PIC_RESTORE_MASK_NONE);
}

// All ISRs will call this handler with a pointer to the registers struct
extern "C" void ISRHANDLER(Registers* reg)
{
    if (reg->interrupt_number < 32)
    {
        if (reg->interrupt_number == 14)
        {
            uint64_t FaultAddress = 0;
            __asm__ __volatile__("mov %%cr2, %0" : "=r"(FaultAddress));

            FrameBufferConsole* ActiveConsole = FrameBufferConsole::GetActive();
            if (ActiveConsole != nullptr)
            {
                ActiveConsole->printf_("Page fault: cr2=%p rip=%p err=%p cs=%p rsp=%p\n", (void*) FaultAddress, (void*) reg->rip, (void*) reg->error_code, (void*) reg->cs, (void*) reg->rsp);
            }

            while (1)
            {
                __asm__ __volatile__("hlt");
            }
        }
    }
    else
    {
        if (reg->interrupt_number >= 32 && reg->interrupt_number <= 47)
        {
            outb(PIC1_COMMAND_PORT, 0x20); // Send End of Interrupt (EOI) signal to master PIC
            if (reg->interrupt_number >= 40)
            {
                outb(PIC2_COMMAND_PORT, 0x20); // Send End of Interrupt (EOI) signal to slave PIC
            }
        }
    }

    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher != nullptr)
    {
        LogicLayer* ActiveLogicLayer = ActiveDispatcher->GetLogicLayer();
        if (ActiveLogicLayer != nullptr)
        {
            ActiveLogicLayer->CaptureCurrentInterruptState(reg);
        }

        ActiveDispatcher->InterruptHandler(reg->interrupt_number);
    }
}

void InitInterrupts()
{
    ISR_init();
    LoadIDT(&IDTDescriptor);

    RemapPIC();
    InitTimer();

    // Enable Interrupts
    asm volatile("sti");
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
    uint64_t KernelInterruptStackTop = reinterpret_cast<uint64_t>(&KernelInterruptStack[sizeof(KernelInterruptStack)]);
    KernelInterruptStackTop          = (KernelInterruptStackTop & ~0xFULL) - 8;

    KernelTSS.RSP0_lower  = static_cast<uint32_t>(KernelInterruptStackTop & 0xFFFFFFFF);
    KernelTSS.RSP0_upper  = static_cast<uint32_t>((KernelInterruptStackTop >> 32) & 0xFFFFFFFF);
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

extern "C" void HandleSystemCallFromEntry(uint64_t Arg1, uint64_t Arg2, uint64_t Arg3, uint64_t Arg4, uint64_t Arg5, uint64_t Arg6, uint64_t SystemCallNumber)
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher != nullptr)
    {
        ActiveDispatcher->HandleSystemCall(SystemCallNumber, Arg1, Arg2, Arg3, Arg4, Arg5, Arg6);
    }
}

extern "C" void SystemCallEntry();

void InitSystemCalls()
{
    uint64_t star = ((uint64_t) USER_CS << SYSCALL_STAR_USER_CS_SHIFT) | ((uint64_t) KERNEL_CS << SYSCALL_STAR_KERNEL_CS_SHIFT);
    WriteMSR(IA32_STAR_MSR, star);
    WriteMSR(IA32_LSTAR_MSR, reinterpret_cast<uint64_t>(&SystemCallEntry));
    WriteMSR(IA32_FMASK_MSR, RFLAGS_IF);

    uint64_t efer = ReadMSR(IA32_EFER_MSR);
    WriteMSR(IA32_EFER_MSR, efer | IA32_EFER_SCE);
}