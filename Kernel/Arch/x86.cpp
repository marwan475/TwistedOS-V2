/**
 * File: x86.cpp
 * Author: Marwan Mostafa
 * Description: x86-64 architecture-specific kernel implementation.
 */

#include <Arch/x86.hpp>
#include <Layers/Dispatcher.hpp>
#include <Logging/FrameBufferConsole.hpp>

#define IA32_EFER_MSR 0xC0000080
#define IA32_STAR_MSR 0xC0000081
#define IA32_LSTAR_MSR 0xC0000082
#define IA32_FMASK_MSR 0xC0000084
#define IA32_FS_BASE_MSR 0xC0000100

#define IA32_EFER_SCE (1ULL << 0)
#define RFLAGS_IF (1ULL << 9)

#define SYSCALL_INTERRUPT_VECTOR 0x80
#define PIC_END_OF_INTERRUPT_COMMAND 0x20

#define SYSCALL_STAR_USER_CS_SHIFT 48
#define SYSCALL_STAR_KERNEL_CS_SHIFT 32

TSS                KernelTSS  = {};
GDT                KernelGDT  = {};
DiscriptorRegister KernelGDTR = {};

static IDTentry       IDT[256]                                                 = {};
static IDTdescription IDTDescriptor                                            = {sizeof(IDT) - 1, IDT};
static uint8_t        KernelInterruptStack[16384] __attribute__((aligned(16))) = {};

extern "C"
{
    extern uint64_t SavedSystemCallUserRSP;
    extern uint64_t SavedSystemCallUserRIP;
    extern uint64_t SavedSystemCallUserRFLAGS;
    extern uint64_t SavedSystemCallUserRAX;
    extern uint64_t SavedSystemCallUserRDX;
    extern uint64_t SavedSystemCallUserRBX;
    extern uint64_t SavedSystemCallUserRBP;
    extern uint64_t SavedSystemCallUserRSI;
    extern uint64_t SavedSystemCallUserRDI;
    extern uint64_t SavedSystemCallUserR8;
    extern uint64_t SavedSystemCallUserR9;
    extern uint64_t SavedSystemCallUserR10;
    extern uint64_t SavedSystemCallUserR12;
    extern uint64_t SavedSystemCallUserR13;
    extern uint64_t SavedSystemCallUserR14;
    extern uint64_t SavedSystemCallUserR15;
}

static Process* GetCurrentProcessForSystemCallFrame()
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher == nullptr)
    {
        return nullptr;
    }

    LogicLayer* ActiveLogicLayer = ActiveDispatcher->GetLogicLayer();
    if (ActiveLogicLayer == nullptr)
    {
        return nullptr;
    }

    ProcessManager* PM = ActiveLogicLayer->GetProcessManager();
    if (PM == nullptr)
    {
        return nullptr;
    }

    return PM->GetCurrentProcess();
}

[[maybe_unused]] static const char* ProcessStateToString(ProcessState State)
{
    switch (State)
    {
        case PROCESS_RUNNING:
            return "running";
        case PROCESS_READY:
            return "ready";
        case PROCESS_BLOCKED:
            return "blocked";
        case PROCESS_TERMINATED:
            return "terminated";
        default:
            return "unknown";
    }
}

[[maybe_unused]] static const char* ProcessLevelToString(ProcessLevel Level)
{
    switch (Level)
    {
        case PROCESS_LEVEL_KERNEL:
            return "kernel";
        case PROCESS_LEVEL_USER:
            return "user";
        default:
            return "unknown";
    }
}

[[maybe_unused]] static const char* ProcessFileTypeToString(FILE_TYPE FileType)
{
    switch (FileType)
    {
        case FILE_TYPE_RAW_BINARY:
            return "raw";
        case FILE_TYPE_ELF:
            return "elf";
        default:
            return "unknown";
    }
}

/**
 * Function: LoadIDT
 * Description: Loads the processor IDT register with the provided descriptor.
 * Parameters:
 *   const IDTdescription* idt_descriptor - Pointer to the IDT descriptor.
 * Returns:
 *   void - No return value.
 */
static inline void LoadIDT(const IDTdescription* idt_descriptor)
{
    __asm__ __volatile__("lidt %0" : : "m"(*idt_descriptor) : "memory");
}

/**
 * Function: outb
 * Description: Writes one byte to a hardware I/O port.
 * Parameters:
 *   uint16_t port - Target I/O port number.
 *   uint8_t value - Byte value to write.
 * Returns:
 *   void - No return value.
 */
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * Function: inb
 * Description: Reads one byte from a hardware I/O port.
 * Parameters:
 *   uint16_t port - Source I/O port number.
 * Returns:
 *   uint8_t - Byte read from the port.
 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t value = 0;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/**
 * Function: outl
 * Description: Writes a 32-bit value to a hardware I/O port.
 * Parameters:
 *   uint16_t port - Target I/O port number.
 *   uint32_t value - Double word value to write.
 * Returns:
 *   void - No return value.
 */
static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ __volatile__("outl %0, %1" : : "a"(value), "Nd"(port));
}

/**
 * Function: inl
 * Description: Reads a 32-bit value from a hardware I/O port.
 * Parameters:
 *   uint16_t port - Source I/O port number.
 * Returns:
 *   uint32_t - Double word read from the port.
 */
static inline uint32_t inl(uint16_t port)
{
    uint32_t value = 0;
    __asm__ __volatile__("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void X86OutB(uint16_t Port, uint8_t Value)
{
    outb(Port, Value);
}

uint8_t X86InB(uint16_t Port)
{
    return inb(Port);
}

void X86OutW(uint16_t Port, uint16_t Value)
{
    __asm__ __volatile__("outw %0, %1" : : "a"(Value), "Nd"(Port));
}

uint16_t X86InW(uint16_t Port)
{
    uint16_t Value = 0;
    __asm__ __volatile__("inw %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

static uint32_t ReadPciConfigDword(uint8_t Bus, uint8_t Device, uint8_t Function, uint8_t RegisterOffset)
{
    constexpr uint16_t PciConfigAddressPort = 0xCF8;
    constexpr uint16_t PciConfigDataPort    = 0xCFC;

    uint32_t Address
            = (1u << 31) | (static_cast<uint32_t>(Bus) << 16) | (static_cast<uint32_t>(Device) << 11) | (static_cast<uint32_t>(Function) << 8) | (static_cast<uint32_t>(RegisterOffset) & 0xFCu);

    outl(PciConfigAddressPort, Address);
    return inl(PciConfigDataPort);
}

bool X86ReadPCIConfigDword(uint8_t Bus, uint8_t Device, uint8_t Function, uint8_t RegisterOffset, uint32_t* Value)
{
    if (Value == nullptr)
    {
        return false;
    }

    if (Device >= 32 || Function >= 8)
    {
        return false;
    }

    *Value = ReadPciConfigDword(Bus, Device, Function, RegisterOffset);
    return true;
}

/**
 * Function: WriteMSR
 * Description: Writes a 64-bit value to a model-specific register.
 * Parameters:
 *   uint32_t msr - MSR index.
 *   uint64_t value - Value to write to the MSR.
 * Returns:
 *   void - No return value.
 */
static inline void WriteMSR(uint32_t msr, uint64_t value)
{
    uint32_t low  = (uint32_t) (value & 0xFFFFFFFF);
    uint32_t high = (uint32_t) ((value >> 32) & 0xFFFFFFFF);
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

/**
 * Function: ReadMSR
 * Description: Reads a 64-bit value from a model-specific register.
 * Parameters:
 *   uint32_t msr - MSR index.
 * Returns:
 *   uint64_t - Value read from the MSR.
 */
static inline uint64_t ReadMSR(uint32_t msr)
{
    uint32_t low  = 0;
    uint32_t high = 0;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t) high << 32) | low;
}

void SetUserFSBase(uint64_t BaseAddress)
{
    WriteMSR(IA32_FS_BASE_MSR, BaseAddress);
}

uint64_t GetUserFSBase()
{
    return ReadMSR(IA32_FS_BASE_MSR);
}

void X86Halt()
{
    __asm__ __volatile__("hlt");
}

uint64_t X86ReadCR3()
{
    uint64_t PageTableAddress = 0;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(PageTableAddress));
    return PageTableAddress;
}

void X86WriteCR3(uint64_t PageMapL4TableAddr)
{
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(PageMapL4TableAddr) : "memory");
}

/**
 * Function: SetIDTEntry
 * Description: Populates one IDT entry with handler address, selector, and flags.
 * Parameters:
 *   int interrupt - Interrupt vector index.
 *   void (*base)() - ISR handler function address.
 *   uint16_t segment - Code segment selector.
 *   uint8_t flags - Gate attribute flags.
 * Returns:
 *   void - No return value.
 */
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

/**
 * Function: EnableIDTEntry
 * Description: Marks an IDT entry as present.
 * Parameters:
 *   int interrupt - Interrupt vector index.
 * Returns:
 *   void - No return value.
 */
static void EnableIDTEntry(int interrupt)
{
    IDT[interrupt].type_attr |= IDT_FLAG_PRESENT;
}

/**
 * Function: DisableIDTEntry
 * Description: Marks an IDT entry as not present.
 * Parameters:
 *   int interrupt - Interrupt vector index.
 * Returns:
 *   void - No return value.
 */
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

/**
 * Function: ISR_init
 * Description: Initializes all IDT vectors and configures user-accessible syscall interrupt gate.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
static void ISR_init()
{
    DisableIDTEntry(0); // Avoid Compiler Warning

    for (int interrupt = 0; interrupt < 256; interrupt++)
    {
        SetIDTEntry(interrupt, ISRHandlers[interrupt], GDT_CODE_SEGMENT, IDT_DEFAULT_GATE_FLAGS);
        EnableIDTEntry(interrupt);
    }

    SetIDTEntry(SYSCALL_INTERRUPT_VECTOR, ISRHandlers[SYSCALL_INTERRUPT_VECTOR], GDT_CODE_SEGMENT, IDT_FLAG_PRESENT | IDT_FLAG_RING3 | IDT_FLAG_GATE_32BIT_INT);
}

/**
 * Function: InitTimer
 * Description: Configures PIT channel 0 for periodic timer interrupts.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
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

/**
 * Function: RemapPIC
 * Description: Remaps master and slave PIC vectors and completes PIC initialization sequence.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
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
/**
 * Function: ISRHANDLER
 * Description: Main interrupt handler that processes faults, sends PIC EOI, and dispatches to active kernel layers.
 * Parameters:
 *   Registers* reg - Pointer to captured interrupt register state.
 * Returns:
 *   void - No return value.
 */
extern "C" void ISRHANDLER(Registers* reg)
{
    if (reg->interrupt_number < 32)
    {
        Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
        if (ActiveDispatcher != nullptr)
        {
            TTY* Terminal = ActiveDispatcher->GetResourceLayer()->GetTTY();
#ifdef DEBUG_BUILD
            if (Terminal != nullptr)
            {
                Terminal->printf_("CPU exception: int=%lu err=%p rip=%p cs=%p rflags=%p rsp=%p\n", reg->interrupt_number, (void*) reg->error_code, (void*) reg->rip, (void*) reg->cs,
                                  (void*) reg->rflags, (void*) reg->rsp);

                Process* CurrentProcess = GetCurrentProcessForSystemCallFrame();
                if (CurrentProcess == nullptr)
                {
                    Terminal->printf_("Exception process: <none>\n");
                }
                else
                {
                    uint64_t ProcessPageTable = 0;
                    uint64_t CodeStart        = 0;
                    uint64_t CodeSize         = 0;
                    uint64_t HeapStart        = 0;
                    uint64_t HeapSize         = 0;
                    uint64_t StackStart       = 0;
                    uint64_t StackSize        = 0;
                    uint64_t ProcessFSBase    = 0;

                    if (CurrentProcess->AddressSpace != nullptr)
                    {
                        ProcessPageTable = CurrentProcess->AddressSpace->GetPageMapL4TableAddr();
                        CodeStart        = CurrentProcess->AddressSpace->GetCodeVirtualAddressStart();
                        CodeSize         = CurrentProcess->AddressSpace->GetCodeSize();
                        HeapStart        = CurrentProcess->AddressSpace->GetHeapVirtualAddressStart();
                        HeapSize         = CurrentProcess->AddressSpace->GetHeapSize();
                        StackStart       = CurrentProcess->AddressSpace->GetStackVirtualAddressStart();
                        StackSize        = CurrentProcess->AddressSpace->GetStackSize();
                    }
                    ProcessFSBase = CurrentProcess->UserFSBase;

                    uint64_t LiveFSBase      = GetUserFSBase();
                    uint64_t ActivePageTable = ActiveDispatcher->GetResourceLayer()->ReadCurrentPageTable();

                    Terminal->printf_("Exception process: id=%u parent=%u state=%s level=%s type=%s waiting_sysret=%u saved_syscall=%u addrspace=%p cr3=%p proc_cr3=%p\n", CurrentProcess->Id,
                                      CurrentProcess->ParrentId, ProcessStateToString(CurrentProcess->Status), ProcessLevelToString(CurrentProcess->Level),
                                      ProcessFileTypeToString(CurrentProcess->FileType), CurrentProcess->WaitingForSystemCallReturn ? 1U : 0U, CurrentProcess->HasSavedSystemCallFrame ? 1U : 0U,
                                      CurrentProcess->AddressSpace, (void*) ActivePageTable, (void*) ProcessPageTable);

                    Terminal->printf_("Exception process FS: live=%p saved=%p\n", (void*) LiveFSBase, (void*) ProcessFSBase);

                    Terminal->printf_("Exception process ranges: code=[%p..%p) heap=[%p..%p) stack=[%p..%p)\n", (void*) CodeStart, (void*) (CodeStart + CodeSize), (void*) HeapStart,
                                      (void*) (HeapStart + HeapSize), (void*) StackStart, (void*) (StackStart + StackSize));
                }
            }
#else
            (void) Terminal;
#endif
        }

        if (reg->interrupt_number == 14)
        {
            uint64_t FaultAddress = 0;
            __asm__ __volatile__("mov %%cr2, %0" : "=r"(FaultAddress));

            if (ActiveDispatcher != nullptr)
            {
                ActiveDispatcher->GetResourceLayer()->GetTTY()->printf_("Page fault: cr2=%p rip=%p err=%p cs=%p rsp=%p\n", (void*) FaultAddress, (void*) reg->rip, (void*) reg->error_code,
                                                                        (void*) reg->cs, (void*) reg->rsp);
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
            outb(PIC1_COMMAND_PORT, PIC_END_OF_INTERRUPT_COMMAND); // Send End of Interrupt (EOI) signal to master PIC
            if (reg->interrupt_number >= 40)
            {
                outb(PIC2_COMMAND_PORT, PIC_END_OF_INTERRUPT_COMMAND); // Send End of Interrupt (EOI) signal to slave PIC
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

/**
 * Function: InitInterrupts
 * Description: Initializes ISR tables, loads IDT, configures PIC/PIT, and enables CPU interrupts.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void InitInterrupts()
{
    ISR_init();
    LoadIDT(&IDTDescriptor);

    RemapPIC();
    InitTimer();

    // Enable Interrupts
    asm volatile("sti");
}

/**
 * Function: BuildTSSDescriptor
 * Description: Builds a TSS descriptor structure for inclusion in the GDT.
 * Parameters:
 *   const TSS* tss - Pointer to the TSS object.
 * Returns:
 *   TSSDescriptor - Encoded descriptor for the provided TSS.
 */
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

/**
 * Function: BuildGDT
 * Description: Builds a complete GDT with kernel/user segments and TSS descriptor.
 * Parameters:
 *   const TSSDescriptor& tss_descriptor - TSS descriptor to embed.
 * Returns:
 *   GDT - Initialized global descriptor table.
 */
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

/**
 * Function: InitGDT
 * Description: Initializes kernel TSS and GDT, then reloads descriptor and segment registers.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
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

/**
 * Function: HandleSystemCallFromEntry
 * Description: Receives syscall register arguments from assembly entry and forwards to dispatcher.
 * Parameters:
 *   uint64_t Arg1 - First syscall argument.
 *   uint64_t Arg2 - Second syscall argument.
 *   uint64_t Arg3 - Third syscall argument.
 *   uint64_t Arg4 - Fourth syscall argument.
 *   uint64_t Arg5 - Fifth syscall argument.
 *   uint64_t Arg6 - Sixth syscall argument.
 *   uint64_t SystemCallNumber - Syscall identifier.
 * Returns:
 *   uint64_t - Syscall return value to place in rax.
 */
extern "C" uint64_t HandleSystemCallFromEntry(uint64_t Arg1, uint64_t Arg2, uint64_t Arg3, uint64_t Arg4, uint64_t Arg5, uint64_t Arg6, uint64_t SystemCallNumber)
{
    Dispatcher* ActiveDispatcher = Dispatcher::GetActive();
    if (ActiveDispatcher != nullptr)
    {
        return static_cast<uint64_t>(ActiveDispatcher->HandleSystemCall(SystemCallNumber, Arg1, Arg2, Arg3, Arg4, Arg5, Arg6));
    }

    return static_cast<uint64_t>(-38);
}

/**
 * Function: GetCurrentProcessCpuStateForSyscallReturn
 * Description: Returns the running process CPU state used by syscall return logic for non-returning execve success.
 * Parameters:
 *   None
 * Returns:
 *   const CpuState* - Pointer to current running process state, or nullptr when unavailable.
 */
extern "C" const CpuState* GetCurrentProcessCpuStateForSyscallReturn()
{
    Process* CurrentProcess = GetCurrentProcessForSystemCallFrame();
    if (CurrentProcess == nullptr)
    {
        return nullptr;
    }

    return &CurrentProcess->State;
}

extern "C" void GetSavedSystemCallFrame(uint64_t* Rip, uint64_t* Rsp, uint64_t* RFlags)
{
    Process* CurrentProcess = GetCurrentProcessForSystemCallFrame();
    if (CurrentProcess == nullptr || !CurrentProcess->HasSavedSystemCallFrame)
    {
        return;
    }

    if (Rip != nullptr)
    {
        *Rip = CurrentProcess->SavedSystemCallFrame.UserRIP;
    }

    if (Rsp != nullptr)
    {
        *Rsp = CurrentProcess->SavedSystemCallFrame.UserRSP;
    }

    if (RFlags != nullptr)
    {
        *RFlags = CurrentProcess->SavedSystemCallFrame.UserRFLAGS;
    }
}

extern "C" void LoadSavedSystemCallCpuState(CpuState* State)
{
    if (State == nullptr)
    {
        return;
    }

    Process* CurrentProcess = GetCurrentProcessForSystemCallFrame();
    if (CurrentProcess == nullptr || !CurrentProcess->HasSavedSystemCallFrame)
    {
        return;
    }

    const ProcessSavedSystemCallFrame& SavedFrame = CurrentProcess->SavedSystemCallFrame;

    State->rax    = SavedFrame.UserRAX;
    State->rcx    = 0;
    State->rdx    = SavedFrame.UserRDX;
    State->rbx    = SavedFrame.UserRBX;
    State->rbp    = SavedFrame.UserRBP;
    State->rsi    = SavedFrame.UserRSI;
    State->rdi    = SavedFrame.UserRDI;
    State->r8     = SavedFrame.UserR8;
    State->r9     = SavedFrame.UserR9;
    State->r10    = SavedFrame.UserR10;
    State->r11    = 0;
    State->r12    = SavedFrame.UserR12;
    State->r13    = SavedFrame.UserR13;
    State->r14    = SavedFrame.UserR14;
    State->r15    = SavedFrame.UserR15;
    State->rip    = SavedFrame.UserRIP;
    State->rflags = SavedFrame.UserRFLAGS;
    State->rsp    = SavedFrame.UserRSP;
    State->cs     = USER_CS;
    State->ss     = USER_SS;
}

extern "C" bool PersistCurrentSavedSystemCallFrame()
{
    Process* CurrentProcess = GetCurrentProcessForSystemCallFrame();
    if (CurrentProcess == nullptr)
    {
        return false;
    }

    CurrentProcess->SavedSystemCallFrame.UserRSP    = SavedSystemCallUserRSP;
    CurrentProcess->SavedSystemCallFrame.UserRIP    = SavedSystemCallUserRIP;
    CurrentProcess->SavedSystemCallFrame.UserRFLAGS = SavedSystemCallUserRFLAGS;
    CurrentProcess->SavedSystemCallFrame.UserRAX    = SavedSystemCallUserRAX;
    CurrentProcess->SavedSystemCallFrame.UserRDX    = SavedSystemCallUserRDX;
    CurrentProcess->SavedSystemCallFrame.UserRBX    = SavedSystemCallUserRBX;
    CurrentProcess->SavedSystemCallFrame.UserRBP    = SavedSystemCallUserRBP;
    CurrentProcess->SavedSystemCallFrame.UserRSI    = SavedSystemCallUserRSI;
    CurrentProcess->SavedSystemCallFrame.UserRDI    = SavedSystemCallUserRDI;
    CurrentProcess->SavedSystemCallFrame.UserR8     = SavedSystemCallUserR8;
    CurrentProcess->SavedSystemCallFrame.UserR9     = SavedSystemCallUserR9;
    CurrentProcess->SavedSystemCallFrame.UserR10    = SavedSystemCallUserR10;
    CurrentProcess->SavedSystemCallFrame.UserR12    = SavedSystemCallUserR12;
    CurrentProcess->SavedSystemCallFrame.UserR13    = SavedSystemCallUserR13;
    CurrentProcess->SavedSystemCallFrame.UserR14    = SavedSystemCallUserR14;
    CurrentProcess->SavedSystemCallFrame.UserR15    = SavedSystemCallUserR15;
    CurrentProcess->HasSavedSystemCallFrame         = true;
    CurrentProcess->WaitingForSystemCallReturn      = true;
    return true;
}

extern "C" bool RestoreCurrentSavedSystemCallFrame()
{
    Process* CurrentProcess = GetCurrentProcessForSystemCallFrame();
    if (CurrentProcess == nullptr || !CurrentProcess->HasSavedSystemCallFrame)
    {
        return false;
    }

    SavedSystemCallUserRSP    = CurrentProcess->SavedSystemCallFrame.UserRSP;
    SavedSystemCallUserRIP    = CurrentProcess->SavedSystemCallFrame.UserRIP;
    SavedSystemCallUserRFLAGS = CurrentProcess->SavedSystemCallFrame.UserRFLAGS;
    SavedSystemCallUserRAX    = CurrentProcess->SavedSystemCallFrame.UserRAX;
    SavedSystemCallUserRDX    = CurrentProcess->SavedSystemCallFrame.UserRDX;
    SavedSystemCallUserRBX    = CurrentProcess->SavedSystemCallFrame.UserRBX;
    SavedSystemCallUserRBP    = CurrentProcess->SavedSystemCallFrame.UserRBP;
    SavedSystemCallUserRSI    = CurrentProcess->SavedSystemCallFrame.UserRSI;
    SavedSystemCallUserRDI    = CurrentProcess->SavedSystemCallFrame.UserRDI;
    SavedSystemCallUserR8     = CurrentProcess->SavedSystemCallFrame.UserR8;
    SavedSystemCallUserR9     = CurrentProcess->SavedSystemCallFrame.UserR9;
    SavedSystemCallUserR10    = CurrentProcess->SavedSystemCallFrame.UserR10;
    SavedSystemCallUserR12    = CurrentProcess->SavedSystemCallFrame.UserR12;
    SavedSystemCallUserR13    = CurrentProcess->SavedSystemCallFrame.UserR13;
    SavedSystemCallUserR14    = CurrentProcess->SavedSystemCallFrame.UserR14;
    SavedSystemCallUserR15    = CurrentProcess->SavedSystemCallFrame.UserR15;
    return true;
}

extern "C" void ApplyCurrentProcessUserFSBaseForSyscallReturn()
{
    Process* CurrentProcess = GetCurrentProcessForSystemCallFrame();
    if (CurrentProcess == nullptr || CurrentProcess->Level != PROCESS_LEVEL_USER)
    {
        return;
    }

    SetUserFSBase(CurrentProcess->UserFSBase);
}

extern "C" void CompleteCurrentSystemCallReturn()
{
    Process* CurrentProcess = GetCurrentProcessForSystemCallFrame();
    if (CurrentProcess == nullptr)
    {
        return;
    }

    CurrentProcess->WaitingForSystemCallReturn = false;
    CurrentProcess->HasSavedSystemCallFrame    = false;
    CurrentProcess->SavedSystemCallFrame       = {};
}

extern "C" void SystemCallEntry();

/**
 * Function: InitSystemCalls
 * Description: Configures STAR/LSTAR/FMASK MSRs and enables SYSCALL instruction support.
 * Parameters:
 *   None
 * Returns:
 *   void - No return value.
 */
void InitSystemCalls()
{
    uint64_t star = ((uint64_t) USER_CS << SYSCALL_STAR_USER_CS_SHIFT) | ((uint64_t) KERNEL_CS << SYSCALL_STAR_KERNEL_CS_SHIFT);
    WriteMSR(IA32_STAR_MSR, star);
    WriteMSR(IA32_LSTAR_MSR, reinterpret_cast<uint64_t>(&SystemCallEntry));
    WriteMSR(IA32_FMASK_MSR, RFLAGS_IF);

    uint64_t efer = ReadMSR(IA32_EFER_MSR);
    WriteMSR(IA32_EFER_MSR, efer | IA32_EFER_SCE);
}