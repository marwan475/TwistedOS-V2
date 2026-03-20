/**
 * File: x86.hpp
 * Author: Marwan Mostafa
 * Description: x86-64 architecture-specific kernel declarations.
 */

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

typedef struct
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) IDTentry;

typedef struct
{
    uint16_t  limit;
    IDTentry* ptr;
} __attribute__((packed)) IDTdescription;

typedef enum
{
    IDT_FLAG_GATE_TASK       = 0x5,
    IDT_FLAG_GATE_16BIT_INT  = 0x6,
    IDT_FLAG_GATE_16BIT_TRAP = 0x7,
    IDT_FLAG_GATE_32BIT_INT  = 0xE,
    IDT_FLAG_GATE_32BIT_TRAP = 0xF,

    IDT_FLAG_RING0 = (0 << 5),
    IDT_FLAG_RING1 = (1 << 5),
    IDT_FLAG_RING2 = (2 << 5),
    IDT_FLAG_RING3 = (3 << 5),

    IDT_FLAG_PRESENT = 0x80,
} IDT_FLAGS;

#define GDT_CODE_SEGMENT 0x08
#define GDT_DATA_SEGMENT 0x10
#define GDT_USER_DATA_SEGMENT 0x20
#define GDT_USER_CODE_SEGMENT 0x18

#define KERNEL_CS GDT_CODE_SEGMENT
#define KERNEL_SS GDT_DATA_SEGMENT
#define USER_CS (GDT_USER_CODE_SEGMENT | 0x3)
#define USER_SS (GDT_USER_DATA_SEGMENT | 0x3)
#define IDT_DEFAULT_GATE_FLAGS (IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE_32BIT_INT)

#define PIC1_COMMAND_PORT 0x20
#define PIC1_DATA_PORT 0x21
#define PIC2_COMMAND_PORT 0xA0
#define PIC2_DATA_PORT 0xA1

#define PIC_INIT_COMMAND 0x11
#define PIC_8086_MODE 0x01
#define PIC1_VECTOR_OFFSET 0x20
#define PIC2_VECTOR_OFFSET 0x28
#define PIC1_CASCADE_IDENTITY 0x04
#define PIC2_CASCADE_IDENTITY 0x02
#define PIC_RESTORE_MASK_NONE 0x00

#define PIT_COMMAND_PORT 0x43
#define PIT_CHANNEL0_DATA_PORT 0x40
#define PIT_CHANNEL0_SQUARE_WAVE 0x36

typedef struct
{
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t cs;
    uint64_t ss;
} CpuState;

static_assert(offsetof(CpuState, rax) == 0, "CpuState::rax offset mismatch");
static_assert(offsetof(CpuState, rip) == 120, "CpuState::rip offset mismatch");
static_assert(offsetof(CpuState, rflags) == 128, "CpuState::rflags offset mismatch");
static_assert(offsetof(CpuState, rsp) == 136, "CpuState::rsp offset mismatch");
static_assert(offsetof(CpuState, cs) == 144, "CpuState::cs offset mismatch");
static_assert(offsetof(CpuState, ss) == 152, "CpuState::ss offset mismatch");
static_assert(sizeof(CpuState) == 160, "CpuState size mismatch");

typedef struct
{
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;

    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint64_t interrupt_number;
    uint64_t error_code;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;

} __attribute__((packed)) Registers;

extern TSS                KernelTSS;
extern GDT                KernelGDT;
extern DiscriptorRegister KernelGDTR;

TSSDescriptor BuildTSSDescriptor(const TSS* tss);
GDT           BuildGDT(const TSSDescriptor& tss_descriptor);
void          InitGDT();
void          InitInterrupts();
void          InitSystemCalls();
void          InitTimer();
void          X86OutB(uint16_t Port, uint8_t Value);
uint8_t       X86InB(uint16_t Port);
void          X86OutW(uint16_t Port, uint16_t Value);
uint16_t      X86InW(uint16_t Port);
bool          X86ReadPCIConfigDword(uint8_t Bus, uint8_t Device, uint8_t Function, uint8_t RegisterOffset, uint32_t* Value);
void          RemapPIC();
void          SetUserFSBase(uint64_t BaseAddress);
uint64_t      GetUserFSBase();
void          X86Halt();
uint64_t      X86ReadCR3();
void          X86WriteCR3(uint64_t PageMapL4TableAddr);

extern "C"
{
#define DECL_ISR(n) void ISR##n();
    DECL_ISR(0)
    DECL_ISR(1)
    DECL_ISR(2)
    DECL_ISR(3)
    DECL_ISR(4)
    DECL_ISR(5)
    DECL_ISR(6)
    DECL_ISR(7)
    DECL_ISR(8)
    DECL_ISR(9)
    DECL_ISR(10)
    DECL_ISR(11)
    DECL_ISR(12)
    DECL_ISR(13)
    DECL_ISR(14)
    DECL_ISR(15)
    DECL_ISR(16)
    DECL_ISR(17)
    DECL_ISR(18)
    DECL_ISR(19)
    DECL_ISR(20)
    DECL_ISR(21)
    DECL_ISR(22)
    DECL_ISR(23)
    DECL_ISR(24)
    DECL_ISR(25)
    DECL_ISR(26)
    DECL_ISR(27)
    DECL_ISR(28)
    DECL_ISR(29)
    DECL_ISR(30)
    DECL_ISR(31)
    DECL_ISR(32)
    DECL_ISR(33)
    DECL_ISR(34)
    DECL_ISR(35)
    DECL_ISR(36)
    DECL_ISR(37)
    DECL_ISR(38)
    DECL_ISR(39)
    DECL_ISR(40)
    DECL_ISR(41)
    DECL_ISR(42)
    DECL_ISR(43)
    DECL_ISR(44)
    DECL_ISR(45)
    DECL_ISR(46)
    DECL_ISR(47)
    DECL_ISR(48)
    DECL_ISR(49)
    DECL_ISR(50)
    DECL_ISR(51)
    DECL_ISR(52)
    DECL_ISR(53)
    DECL_ISR(54)
    DECL_ISR(55)
    DECL_ISR(56)
    DECL_ISR(57)
    DECL_ISR(58)
    DECL_ISR(59)
    DECL_ISR(60)
    DECL_ISR(61)
    DECL_ISR(62)
    DECL_ISR(63)
    DECL_ISR(64)
    DECL_ISR(65)
    DECL_ISR(66)
    DECL_ISR(67)
    DECL_ISR(68)
    DECL_ISR(69)
    DECL_ISR(70)
    DECL_ISR(71)
    DECL_ISR(72)
    DECL_ISR(73)
    DECL_ISR(74)
    DECL_ISR(75)
    DECL_ISR(76)
    DECL_ISR(77)
    DECL_ISR(78)
    DECL_ISR(79)
    DECL_ISR(80)
    DECL_ISR(81)
    DECL_ISR(82)
    DECL_ISR(83)
    DECL_ISR(84)
    DECL_ISR(85)
    DECL_ISR(86)
    DECL_ISR(87)
    DECL_ISR(88)
    DECL_ISR(89)
    DECL_ISR(90)
    DECL_ISR(91)
    DECL_ISR(92)
    DECL_ISR(93)
    DECL_ISR(94)
    DECL_ISR(95)
    DECL_ISR(96)
    DECL_ISR(97)
    DECL_ISR(98)
    DECL_ISR(99)
    DECL_ISR(100)
    DECL_ISR(101)
    DECL_ISR(102)
    DECL_ISR(103)
    DECL_ISR(104)
    DECL_ISR(105)
    DECL_ISR(106)
    DECL_ISR(107)
    DECL_ISR(108)
    DECL_ISR(109)
    DECL_ISR(110)
    DECL_ISR(111)
    DECL_ISR(112)
    DECL_ISR(113)
    DECL_ISR(114)
    DECL_ISR(115)
    DECL_ISR(116)
    DECL_ISR(117)
    DECL_ISR(118)
    DECL_ISR(119)
    DECL_ISR(120)
    DECL_ISR(121)
    DECL_ISR(122)
    DECL_ISR(123)
    DECL_ISR(124)
    DECL_ISR(125)
    DECL_ISR(126)
    DECL_ISR(127)
    DECL_ISR(128)
    DECL_ISR(129)
    DECL_ISR(130)
    DECL_ISR(131)
    DECL_ISR(132)
    DECL_ISR(133)
    DECL_ISR(134)
    DECL_ISR(135)
    DECL_ISR(136)
    DECL_ISR(137)
    DECL_ISR(138)
    DECL_ISR(139)
    DECL_ISR(140)
    DECL_ISR(141)
    DECL_ISR(142)
    DECL_ISR(143)
    DECL_ISR(144)
    DECL_ISR(145)
    DECL_ISR(146)
    DECL_ISR(147)
    DECL_ISR(148)
    DECL_ISR(149)
    DECL_ISR(150)
    DECL_ISR(151)
    DECL_ISR(152)
    DECL_ISR(153)
    DECL_ISR(154)
    DECL_ISR(155)
    DECL_ISR(156)
    DECL_ISR(157)
    DECL_ISR(158)
    DECL_ISR(159)
    DECL_ISR(160)
    DECL_ISR(161)
    DECL_ISR(162)
    DECL_ISR(163)
    DECL_ISR(164)
    DECL_ISR(165)
    DECL_ISR(166)
    DECL_ISR(167)
    DECL_ISR(168)
    DECL_ISR(169)
    DECL_ISR(170)
    DECL_ISR(171)
    DECL_ISR(172)
    DECL_ISR(173)
    DECL_ISR(174)
    DECL_ISR(175)
    DECL_ISR(176)
    DECL_ISR(177)
    DECL_ISR(178)
    DECL_ISR(179)
    DECL_ISR(180)
    DECL_ISR(181)
    DECL_ISR(182)
    DECL_ISR(183)
    DECL_ISR(184)
    DECL_ISR(185)
    DECL_ISR(186)
    DECL_ISR(187)
    DECL_ISR(188)
    DECL_ISR(189)
    DECL_ISR(190)
    DECL_ISR(191)
    DECL_ISR(192)
    DECL_ISR(193)
    DECL_ISR(194)
    DECL_ISR(195)
    DECL_ISR(196)
    DECL_ISR(197)
    DECL_ISR(198)
    DECL_ISR(199)
    DECL_ISR(200)
    DECL_ISR(201)
    DECL_ISR(202)
    DECL_ISR(203)
    DECL_ISR(204)
    DECL_ISR(205)
    DECL_ISR(206)
    DECL_ISR(207)
    DECL_ISR(208)
    DECL_ISR(209)
    DECL_ISR(210)
    DECL_ISR(211)
    DECL_ISR(212)
    DECL_ISR(213)
    DECL_ISR(214)
    DECL_ISR(215)
    DECL_ISR(216)
    DECL_ISR(217)
    DECL_ISR(218)
    DECL_ISR(219)
    DECL_ISR(220)
    DECL_ISR(221)
    DECL_ISR(222)
    DECL_ISR(223)
    DECL_ISR(224)
    DECL_ISR(225)
    DECL_ISR(226)
    DECL_ISR(227)
    DECL_ISR(228)
    DECL_ISR(229)
    DECL_ISR(230)
    DECL_ISR(231)
    DECL_ISR(232)
    DECL_ISR(233)
    DECL_ISR(234)
    DECL_ISR(235)
    DECL_ISR(236)
    DECL_ISR(237)
    DECL_ISR(238)
    DECL_ISR(239)
    DECL_ISR(240)
    DECL_ISR(241)
    DECL_ISR(242)
    DECL_ISR(243)
    DECL_ISR(244)
    DECL_ISR(245)
    DECL_ISR(246)
    DECL_ISR(247)
    DECL_ISR(248)
    DECL_ISR(249)
    DECL_ISR(250)
    DECL_ISR(251)
    DECL_ISR(252)
    DECL_ISR(253)
    DECL_ISR(254)
    DECL_ISR(255)
#undef DECL_ISR

    void GetSavedSystemCallFrame(uint64_t* Rip, uint64_t* Rsp, uint64_t* RFlags);
    void LoadSavedSystemCallCpuState(CpuState* State);
    bool PersistCurrentSavedSystemCallFrame();
    bool RestoreCurrentSavedSystemCallFrame();
    void CompleteCurrentSystemCallReturn();
}
