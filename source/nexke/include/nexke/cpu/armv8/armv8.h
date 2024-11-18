/*
    armv8.h - contains nexke armv8 stuff
    Copyright 2023 - 2024 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#ifndef _ARMV8_H
#define _ARMV8_H

#define MM_PAGE_TABLES

#include <nexke/types.h>
#include <stdbool.h>
#include <stdint.h>

typedef uint64_t paddr_t;

// SPR functions
#define CpuReadSpr(spr)                              \
    ({                                               \
        uint64_t __tmp = 0;                          \
        asm volatile ("mrs %0, " spr : "=r"(__tmp)); \
        __tmp;                                       \
    })

#define CpuWriteSpr(spr, val) asm volatile ("msr " spr ", %0" : : "r"(val));

typedef struct _nkarchccb
{
    uint64_t features;    // CPU feature flags
    bool intHold;         // Interrupt hold state
    bool intReq;          // Interrupt enable request
    int asidBits;         // Number of ASID bits
    int paBits;           // Physical address bits
    int vaBits;           // Virtual address bits supported
} NkArchCcb_t;

// Defined CPU features
#define CPU_FEATURE_RNG            (1 << 0)
#define CPU_FEATURE_ATOMIC         (1 << 1)
#define CPU_FEATURE_CRC32          (1 << 2)
#define CPU_FEATURE_XS             (1 << 3)
#define CPU_FEATURE_EXS            (1 << 4)
#define CPU_FEATURE_TGRAN4         (1 << 5)
#define CPU_FEATURE_TGRAN64        (1 << 6)
#define CPU_FEATURE_TGRAN16        (1 << 7)
#define CPU_FEATURE_MIX_ENDIAN_EL0 (1 << 8)
#define CPU_FEATURE_MIX_ENDIAN     (1 << 9)
#define CPU_FEATURE_SECURE_MEM     (1 << 10)
#define CPU_FEATURE_E0PD           (1 << 11)
#define CPU_FEATURE_CNP            (1 << 12)
#define CPU_FEATURE_GIC3_4         (1 << 13)
#define CPU_FEATURE_GIC41          (1 << 14)
#define CPU_FEATURE_EL0_AA32       (1 << 15)
#define CPU_FEATURE_NMI            (1 << 16)
#define CPU_NUM_FEATURES           17

// ID register bits
#define CPU_FEAT_MASK       0xF
#define CPU_ISAR0_RNDR      60ULL
#define CPU_ISAR0_ATOMIC    20
#define CPU_ISAR0_CRC32     16
#define CPU_ISAR1_XS        56ULL
#define CPU_MMFR0_EXS       44ULL
#define CPU_MMFR0_TGRAN4    28
#define CPU_MMFR0_TGRAN64   24
#define CPU_MMFR0_TGRAN16   20
#define CPU_MMFR0_BIGENDEL0 16
#define CPU_MMFR0_SNSMEM    12
#define CPU_MMFR0_BIGEND    8
#define CPU_MMFR0_ASIDBITS  4
#define CPU_MMFR0_PABITS    0
#define CPU_MMFR2_E0PD      60ULL
#define CPU_MMFR2_CNP       0
#define CPU_MMFR2_VABITS    16
#define CPU_PFR0_GIC        24
#define CPU_PFR0_EL0_AA32   0
#define CPU_PFR1_NMI        36ULL

// MIDR register bits
#define CPU_PARTNUM_MASK  0xFFF
#define CPU_PARTNUM_SHIFT 4
#define CPU_ARCH_SHIFT    16
#define CPU_VARIANT_SHIFT 20
#define CPU_IMPL_SHIFT    24
#define CPU_IMPL_MASK     0xFF

// Implementers
#define CPU_IMPL_ARM                    0x41
#define CPU_IMPL_BROADCOM               0x42
#define CPU_IMPL_CAVIUM                 0x43
#define CPU_IMPL_DEC                    0x44
#define CPU_IMPL_FUJISTU                0x46
#define CPU_IMPL_INFINEON               0x49
#define CPU_IMPL_MOTOROLA               9x4D
#define CPU_IMPL_NVIDIA                 0x4E
#define CPU_IMPL_APPLIED_MICRO_CIRCUITS 0x50
#define CPU_IMPL_QUALCOMM               0x51
#define CPU_IMPL_MARVELL                0x56
#define CPU_IMPL_INTEL                  0x69
#define CPU_IMPL_AMPERE                 0xC0

NkCcb_t* CpuRealCcb();

extern bool ccbInit;

// Gets the current CCB
static inline NkCcb_t* CpuGetCcb()
{
    if (ccbInit)
    {
        return (NkCcb_t*) CpuReadSpr ("TPIDR_EL1");
    }
    else
        return CpuRealCcb();
}

void __attribute__ ((noreturn)) CpuCrash();

// CPU page size
#define NEXKE_CPU_PAGESZ     0x1000
#define NEXKE_CPU_PAGE_SHIFT 12

// User address end
#ifndef NEXNIX_X86_64_LA57
#define NEXKE_USER_ADDR_END 0x7FFFFFFFFFFF
#else
#define NEXKE_USER_ADDR_END 0xFFFFFFFFFFFFFF
#endif

#define NEXKE_KERNEL_BASE 0xFFFFFFFF80000000

// Kernel general allocation start
#define NEXKE_KERNEL_ADDR_START 0xFFFFFFFFC0000000
#define NEXKE_KERNEL_ADDR_END   0xFFFFFFFFDFFFFFFF

// Framebuffer locations
#define NEXKE_FB_BASE      0xFFFFFFFFF0000000
#define NEXKE_BACKBUF_BASE 0xFFFFFFFFE0000000

// PFN map base
#define NEXKE_PFNMAP_BASE 0xFFFFFFF000000000
#define NEXKE_PFNMAP_MAX  (0xE80000000 - 0x10)

#define NEXKE_SERIAL_MMIO_BASE 0xFFFFFFFF90000000

#define CpuSpin() asm volatile ("yield")

// Gets current exception level
int CpuGetEl();

// DAIF helpers
#define CPU_ARMV8_INT_D (1 << 9)
#define CPU_ARMV8_INT_A (1 << 8)
#define CPU_ARMV8_INT_I (1 << 7)
#define CPU_ARMV8_INT_F (1 << 6)

// CPU data structures
typedef struct _cputhread
{

} CpuThread_t;

typedef struct _cpuint
{
    uint64_t tpidr, spsr, elr, handler, spEl0;
    uint64_t x30, x29, x28, x27, x26, x25, x24;
    uint64_t x23, x22, x21, x20, x19, x18, x17, x16;
    uint64_t x15, x14, x13, x12, x11, x10, x9, x8;
    uint64_t x7, x6, x5, x4, x3, x2, x1, x0;
} CpuIntContext_t;

// Returns interrupt number of currently executing interrupts based on ctx
int CpuGetIntNum (CpuIntContext_t* ctx);

#define CPU_CTX_INTNUM(ctx) (CpuGetIntNum (ctx))

typedef struct _cpuctx
{

} CpuContext_t;

// Stubs of I/O port functions as they are referenced in some modules
// but invalid on ARMv8

void __attribute__ ((noreturn)) NkPanic (const char* fmt, ...);

static inline uint8_t CpuInb (uint16_t port)
{
    NkPanic ("Attempt to use PC I/O on ARM!");
    return 0;
}

static inline uint16_t CpuInw (uint16_t port)
{
    NkPanic ("Attempt to use PC I/O on ARM!");
    return 0;
}

static inline uint32_t CpuInl (uint16_t port)
{
    NkPanic ("Attempt to use PC I/O on ARM!");
    return 0;
}

static inline void CpuOutb (uint16_t port, uint8_t val)
{
    NkPanic ("Attempt to use PC I/O on ARM!");
}

static inline void CpuOutw (uint16_t port, uint16_t val)
{
    NkPanic ("Attempt to use PC I/O on ARM!");
}

static inline void CpuOutl (uint16_t port, uint32_t val)
{
    NkPanic ("Attempt to use PC I/O on ARM!");
}

// Interrupt stuff
#define NK_MAX_INTS \
    256    // Technically this is arbitary. ARM only has a couple of interrupt vectors
           // and we have to figure out the table index based on the ARM vector
#define CPU_BASE_HWINT 64

// Vector table
extern uint8_t CpuVectorTable[];

// Vector numbers
#define CPU_EXEC_SP_EL0     0
#define CPU_EXEC_SP_ELX     4
#define CPU_EXEC_LOWER_EL   8
#define CPU_EXEC_LOWER_EL32 12

#define CPU_IS_EXEC(handler)                                         \
    ((handler) == CPU_EXEC_SP_EL0 || (handler) == CPU_EXEC_SP_ELX || \
     (handler) == CPU_EXEC_LOWER_EL || (handler) == CPU_EXEC_LOWER_EL32)

#define CPU_IRQ_SP_EL0     1
#define CPU_IRQ_SP_ELX     5
#define CPU_IRQ_LOWER_EL   9
#define CPU_IRQ_LOWER_EL32 13

#define CPU_IS_IRQ(handler)                                        \
    ((handler) == CPU_IRQ_SP_EL0 || (handler) == CPU_IRQ_SP_ELX || \
     (handler) == CPU_IRQ_LOWER_EL || (handler) == CPU_IRQ_LOWER_EL32)

#define CPU_FIQ_SP_EL0     2
#define CPU_FIQ_SP_ELX     6
#define CPU_FIQ_LOWER_EL   10
#define CPU_FIQ_LOWER_EL32 14

#define CPU_IS_FIQ(handler)                                        \
    ((handler) == CPU_FIQ_SP_EL0 || (handler) == CPU_FIQ_SP_ELX || \
     (handler) == CPU_FIQ_LOWER_EL || (handler) == CPU_FIQ_LOWER_EL32)

#define CPU_SERR_SP_EL0     3
#define CPU_SERR_SP_ELX     7
#define CPU_SERR_LOWER_EL   11
#define CPU_SERR_LOWER_EL32 15

#define CPU_IS_SERR(handler)                                         \
    ((handler) == CPU_SERR_SP_EL0 || (handler) == CPU_SERR_SP_ELX || \
     (handler) == CPU_SERR_LOWER_EL || (handler) == CPU_SERR_LOWER_EL32)

// Exception Syndrome Register defines
#define CPU_ESR_ISS_MASK   0x1FFFFFF
#define CPU_ESR_IL         (1 << 25)
#define CPU_ESR_EC_SHIFT   26
#define CPU_ESR_EC_MASK    0x3F
#define CPU_ESR_ISS2_MASK  0xFFFFFF
#define CPU_ESR_ISS2_SHIFT 32

// ISS bits

// Data abort
#define CPU_ISS_DA_ISV       (1 << 24)
#define CPU_ISS_DA_SAS_SHIFT 22
#define CPU_ISS_DA_WNR       (1 << 6)
#define CPU_ISS_DFSC_MASK    0x3F

// Instruction abort
#define CPU_ISS_IA_IFSC 0x3F

// Exception codes
#define CPU_EC_UNKNOWN       0
#define CPU_EC_TRAPPED_WF    1
#define CPU_EC_TRAPPED_MCR   3
#define CPU_EC_TRAPPED_MCR2  4
#define CPU_EC_TRAPPED_MCR3  5
#define CPU_EC_TRAPPED_LDC   6
#define CPU_EC_TRAPPED_SIMD  7
#define CPU_EC_TRAPPED_LD64B 10
#define CPU_EC_TRAPPED_MRRC  12
#define CPU_EC_TRAPPED_BTI   13
#define CPU_EC_ILL_STATE     14
#define CPU_EC_AA32_SVC      17
#define CPU_EC_TRAPPED_MSRR  20
#define CPU_EC_TRAPPED_SVC   21
#define CPU_EC_TRAPPED_MSR   24
#define CPU_EC_TRAPPED_SVE   25
#define CPU_EC_TRAPPED_TME   27
#define CPU_EC_PAC_FAIL      28
#define CPU_EC_TRAPPED_SME   29
#define CPU_EC_IA_LOW_EL     32
#define CPU_EC_IA_CUR_EL     33
#define CPU_EC_PC_ALIGN      34
#define CPU_EC_DA_LOW_EL     36
#define CPU_EC_DA_CUR_EL     37
#define CPU_EC_SP_ALIGN      38
#define CPU_EC_MOP_EXEC      39
#define CPU_EC_FPU_AA32      40
#define CPU_EC_FPU_AA64      44
#define CPU_EC_GCS           45
#define CPU_EC_SERR          47
#define CPU_EC_BRK_LOW_EL    48
#define CPU_EC_BRK_CUR_EL    49
#define CPU_EC_SS_LOW_EL     50
#define CPU_EC_SS_CUR_EL     51
#define CPU_EC_WATCH_LOW_EL  52
#define CPU_EC_WATCH_CUR_EL  53
#define CPU_EC_BRKPT_AA32    56
#define CPU_EC_BRK           60
#define CPU_EC_PMU           61
#define CPU_EC_NUM           64

// SCTLR
#define CPU_SCTLR_MMU_EN     (1 << 0)
#define CPU_SCTLR_ALIGN_CHK  (1 << 1)
#define CPU_SCTLR_CACHE      (1 << 2)
#define CPU_SCTLR_SP_ALIGN   (1 << 3)
#define CPU_SCTLR_SP0_ALIGN  (1 << 4)
#define CPU_SCTLR_UMA        (1 << 9)
#define CPU_SCTLR_EOS        (1 << 11)
#define CPU_SCTLR_INST_CACHE (1 << 12)
#define CPU_SCTLR_WFI_EL0    (1 << 13)
#define CPU_SCTLR_WXN        (1 << 19)
#define CPU_SCTLR_EIS        (1 << 22)
#define CPU_SCTLR_PAN        (1 << 23)
#define CPU_SCTLR_EL0_ENDIAN (1 << 24)
#define CPU_SCTLR_ENDIAN     (1 << 25)
#define CPU_SCTLR_NMI        (1ULL << 61)
#define CPU_SCTLR_SPINTMASK  (1ULL << 62)

#include <nexke/cpu/armv8/mul.h>

#endif
