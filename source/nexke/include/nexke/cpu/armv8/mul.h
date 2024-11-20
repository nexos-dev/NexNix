/*
    mul.h - contains MUL header
    Copyright 2024 The NexNix Project

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

#ifndef _MUL_H
#define _MUL_H

#include <stdint.h>

// General PTE type
typedef uint64_t pte_t;

// FIXME: This module currently only supports 48-bit addresses, whereas
// ARMv8 theoretically allows 52-bit addresses. This is for simplicity's sake

// Virtual address manipulating macros
// Shift table for each level
static uint8_t idxShiftTab[] = {0, 12, 21, 30, 39, 48};

// Macro to get level
#define MUL_IDX_MASK               0x1FF
#define MUL_IDX_LEVEL(addr, level) (((addr) >> idxShiftTab[(level)]) & (MUL_IDX_MASK))

// Page entry flags
#define PF_V                   (1ULL << 0)
#define PF_PG                  (1ULL << 1)
#define PF_TAB                 (1ULL << 1)
#define PF_MAIR_NORMAL         (0 << 2)
#define PF_MAIR_DEV            (1 << 2)
#define PF_MAIR_WT             (2 << 2)
#define PF_MAIR_UC             (3 << 2)
#define PF_MAIR_WC             (4 << 2)
#define PF_RO                  (1ULL << 7)
#define PF_EL0                 (1ULL << 6)
#define PF_PXN                 (1ULL << 53)
#define PF_UXN                 (1ULL << 54)
#define PF_AF                  (1ULL << 10)
#define PF_ISH                 (3 << 8)
#define PF_OSH                 (2 << 8)
#define PF_NSH                 (0 << 8)
#define PF_F                   (1ULL << 55)
#define PT_FRAME               0xFFFFFFFFF000
#define PT_GETFRAME(pt)        ((pt) & (PT_FRAME))
#define PF_SETFRAME(pt, frame) ((pt) |= ((frame) & (PT_FRAME)))

// Value to mask with to get non-canonical address
#define MUL_CANONICAL_MASK 0x0000FFFFFFFFFFFF
#define MUL_TOP_ADDR_BIT   (1ULL << 47)
#define MUL_CANONICAL_VAL  0xFFFF000000000000

// Canonical bit
#define MUL_CANONICAL_BIT (1ULL << 55)

// Max level of page tables
#define MM_PTAB_MAX_LEVEL 4

// PT cache defines
#define MUL_MAX_PTCACHE        85
#define MUL_PTCACHE_BASE       0xFFFF00200000
#define MUL_PTCACHE_TABLE_BASE 0xFFFF00001000
#define MUL_PTCACHE_ENTRY_BASE 0xFFFF00000000

// Obtains PTE address of specified PT cache entry
static inline pte_t* MmMulGetCacheAddr (uintptr_t addr)
{
    return (pte_t*) ((MUL_IDX_LEVEL (addr, 1) * sizeof (pte_t)) + MUL_PTCACHE_TABLE_BASE);
}

// Maps a cache entry
static inline void MmMulMapCacheEntry (pte_t* pte, paddr_t tab)
{
    *pte = tab | PF_V | PF_PG | PF_AF | PF_PXN | PF_UXN | PF_OSH | PF_MAIR_NORMAL;
}

#define MmMulFlushCacheEntry MmMulFlush

// Validates that we can map pte2 to pte1
void MmMulVerify (pte_t pte1, pte_t pte2);

typedef struct _memspace MmSpace_t;

// Allocates page table into ent
paddr_t MmMulAllocTable (MmSpace_t* space, uintptr_t addr, pte_t* stBase, pte_t* ent);

// Checks if address is a kernel address
#define MmMulIsKernel(addr) ((addr) >= NEXKE_KERNEL_BASE)

// Flushes whole TLB
void MmMulFlushTlb();

// MAIR defines
#define MUL_MAIR0 0
#define MUL_MAIR1 8
#define MUL_MAIR2 16
#define MUL_MAIR3 24
#define MUL_MAIR4 32
#define MUL_MAIR5 40
#define MUL_MAIR6 48
#define MUL_MAIR7 56

#define MUL_MAIR_DEVICE    0ULL
#define MUL_MAIR_DEVICE_WC 0xCULL
#define MUL_MAIR_NORMAL    0xFFULL
#define MUL_MAIR_WT        0xBBULL
#define MUL_MAIR_NON_CACHE 0x44ULL

// TCR bits
#define MUL_TCR_T0SZ     0
#define MUL_TCR_TSZ_MASK 0x1F
#define MUL_TCR_E0PD     (1 << 7)
#define MUL_TCR_IRGN0    8
#define MUL_TCR_ORGN0    10
#define MUL_TCR_SH0      12
#define MUL_TCR_FIELD    0x3
#define MUL_TCR_TG0      14
#define MUL_TCR_T1SZ     16
#define MUL_TCR_A1       (1 << 22)
#define MUL_TCR_EPD1     (1 << 23)
#define MUL_TCR_IRGN1    24
#define MUL_TCR_ORGN1    26
#define MUL_TCR_SH1      28
#define MUL_TCR_TG1      30
#define MUL_TCR_IPS      32
#define MUL_TCR_IPS_MASK 0x7
#define MUL_TCR_ASID16   (1ULL << 36)
#define MUL_TCR_HA       (1ULL << 39)
#define MUL_TCR_HD       (1ULL << 40)

#define MUL_TCR_IPS_32  0
#define MUL_TCR_IPS_36  1
#define MUL_TCR_IPS_40  2
#define MUL_TCR_IPS_42  3
#define MUL_TCR_IPS_44  4
#define MUL_TCR_IPS_48  5
#define MUL_TCR_IPS_52  6
#define MUL_TCR_MAX_IPS 5    // No 52 bit addresses for now

static int mulIpsToBits[] = {32, 36, 40, 42, 44, 48};

#endif
