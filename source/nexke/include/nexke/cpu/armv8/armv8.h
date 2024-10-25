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

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t paddr_t;

typedef struct _nkarchccb
{
    uint64_t features;    // CPU feature flags
    bool intHold;         // Interrupt hold state
    bool intReq;          // Interrupt enable request
} NkArchCcb_t;

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
#define NEXKE_PFNMAP_MAX  (0xF00000000 - 0x10)

#define NEXKE_SERIAL_MMIO_BASE 0xFFFFFFFF90000000

// MSR functions
#define CpuReadSpr(spr)                              \
    ({                                               \
        uint64_t __tmp = 0;                          \
        asm volatile ("mrs %0, " spr : "=r"(__tmp)); \
        __tmp;                                       \
    })

#define CpuWriteSpr(spr, val) asm volatile ("msr " spr ", %0" : : "r"(val));

// Gets current exception level
int CpuGetEl();

// Hint to CPU we are spinning
void CpuSpin();

// DAIF helpers
#define CPU_ARMV8_INT_D (1 << 9)
#define CPU_ARMV8_INT_A (1 << 8)
#define CPU_ARMV8_INT_I (1 << 7)
#define CPU_ARMV8_INF_F (1 << 6)

// CPU data structures
typedef struct _cputhread
{

} CpuThread_t;

#endif
