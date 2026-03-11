
#include <FrameBufferConsole.hpp>
#include <stddef.h>
#include <stdint.h>
#include <uefi.hpp>

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
} KernelParameters;

// Used to load GDT and TSS
typedef struct
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) DiscriptorRegister;

// GDT is an array of these
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
            uint64_t s : 1;   // System descriptor (0), Code/Data segment (1)
            uint64_t dpl : 2; // Descriptor Privelege Level
            uint64_t p : 1;   // Present flag
            uint64_t limit_19_16 : 4;
            uint64_t avl : 1; // Available for use
            uint64_t l : 1;   // 64 bit (Long mode) code segment
            uint64_t d_b : 1; // Default op size/stack pointer size/upper bound flag
            uint64_t g : 1;   // Granularity; 1 byte (0), 4KiB (1)
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

// Example GDT
typedef struct
{
    X86_64Descriptor null;           // Offset 0x00
    X86_64Descriptor kernel_code_64; // Offset 0x08
    X86_64Descriptor kernel_data_64; // Offset 0x10
    X86_64Descriptor user_code_64;   // Offset 0x18
    X86_64Descriptor user_data_64;   // Offset 0x20
    TSSDescriptor    tss;            // Offset 0x28
} GDT;

static TSS               KernelTSS       = {};
static GDT               KernelGDT       = {};
static DiscriptorRegister KernelGDTR     = {};

static TSSDescriptor BuildTSSDescriptor(const TSS* tss)
{
    uint64_t tss_address = (uint64_t) tss;
    uint32_t tss_limit   = (uint32_t) (sizeof(TSS) - 1);

    TSSDescriptor tss_descriptor          = {};
    tss_descriptor.Descriptor.value       = 0;

    tss_descriptor.Descriptor.value |= (uint64_t) (tss_limit & 0xFFFF);             // limit[15:0]
    tss_descriptor.Descriptor.value |= (uint64_t) (tss_address & 0xFFFFFF) << 16;    // base[23:0]
    tss_descriptor.Descriptor.value |= (uint64_t) 0x9 << 40;                         // type = available 64-bit TSS
    tss_descriptor.Descriptor.value |= (uint64_t) 1 << 47;                           // present
    tss_descriptor.Descriptor.value |= (uint64_t) ((tss_limit >> 16) & 0xF) << 48;   // limit[19:16]
    tss_descriptor.Descriptor.value |= (uint64_t) ((tss_address >> 24) & 0xFF) << 56; // base[31:24]

    tss_descriptor.base_high              = (uint32_t) ((tss_address >> 32) & 0xFFFFFFFF);
    tss_descriptor.reserved               = 0;

    return tss_descriptor;
}

static GDT BuildGDT(const TSSDescriptor& tss_descriptor)
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

static_assert(sizeof(DiscriptorRegister) == 10, "GDTR must be 10 bytes in long mode");
static_assert(sizeof(TSSDescriptor) == 16, "TSS descriptor must be 16 bytes");
static_assert(offsetof(GDT, tss) == 0x28, "TSS selector offset must be 0x28");

extern "C" void EFIAPI kernel_main(KernelParameters KernelArgs) __attribute__((section(".text.entry")));

// Uefi sets us up in 64bit long mode with identity mapped pages
extern "C"
{

    void EFIAPI kernel_main(KernelParameters KernelArgs)
    {
        FrameBufferConsole Console;
        Console.Initialize((uint32_t*) KernelArgs.GopMode.FrameBufferBase,
                           KernelArgs.GopMode.Info->HorizontalResolution, KernelArgs.GopMode.Info->VerticalResolution,
                           KernelArgs.GopMode.Info->PixelsPerScanLine);
        Console.Clear();
        FrameBufferConsole::SetActive(&Console);

        Console.printf_("Hello kernel\n");
        Console.printf_("Framebuffer console working\n");
        Console.printf_("Testing integers: %d, %u, %ld, %lu\n", -42, 42U, 123456789L, 987654321UL);
        Console.printf_("Testing hex: 0x%x, 0x%lx\n", 0xDEADBEAF, 0xCAFEBABECAFEBABEUL);
        Console.printf_("Testing pointer: %p\n", (void*) 0x12345678ABCDEF00);
        Console.printf_("Testing characters: %c %c %c\n", 'A', 'B', 'C');
        Console.printf_("Testing string: %s\n", "Framebuffer printf works!");

        // Set up GDT

        KernelTSS.io_map_base = sizeof(TSS); // No IO bitmap, so set base to end of TSS

        TSSDescriptor tss_descriptor = BuildTSSDescriptor(&KernelTSS);
        KernelGDT                   = BuildGDT(tss_descriptor);

        // Install GDT
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

        Console.printf_("GDT/TSS/segment reload complete\n");

        while (1)
            __asm__ __volatile__("hlt");
    }
}
