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
#define PF_RO                  (1ULL << 7)
#define PF_EL0                 (1ULL << 8)
#define PF_XN                  (1ULL << 54)
#define PF_AF                  (1ULL << 10)
#define PF_FRAME               0xFFFFFFFFF000
#define PT_GETFRAME(pt)        ((pt) & (PF_FRAME))
#define PF_SETFRAME(pt, frame) ((pt) |= ((frame) & (PF_FRAME)))

// Value to mask with to get non-canonical address
#define MUL_CANONICAL_MASK 0x0000FFFFFFFFFFFF

// Canonical bit
#define MUL_CANONICAL_BIT (1ULL << 47)

// Max level of page tables
#define MM_PTAB_MAX_LEVEL 4

// PT cache defines
#define MUL_MAX_PTCACHE        85
#define MUL_PTCACHE_BASE       0xFFFFFFFF00200000
#define MUL_PTCACHE_TABLE_BASE 0xFFFFFFFF00001000
#define MUL_PTCACHE_ENTRY_BASE 0xFFFFFFFF00000000

// Obtains PTE address of specified PT cache entry
static inline pte_t* MmMulGetCacheAddr (uintptr_t addr)
{
    return (pte_t*) ((MUL_IDX_LEVEL (addr, 1) * sizeof (pte_t)) + MUL_PTCACHE_TABLE_BASE);
}

// Maps a cache entry
static inline void MmMulMapCacheEntry (pte_t* pte, paddr_t tab)
{
}

#define MmMulFlushCacheEntry MmMulFlush

// Validates that we can map pte2 to pte1
void MmMulVerify (pte_t pte1, pte_t pte2);

typedef struct _memspace MmSpace_t;

// Allocates page table into ent
paddr_t MmMulAllocTable (MmSpace_t* space, uintptr_t addr, pte_t* stBase, pte_t* ent);

// Checks if address is a kernel address
#define MmMulIsKernel(addr) ((addr) >= NEXKE_KERNEL_BASE)

#endif
