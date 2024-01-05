/*
    cpu.h - contains CPU specific abstractions
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

#ifndef _NB_CPU_H
#define _NB_CPU_H

#include <stdint.h>

#define NEXBOOT_CPU_PAGE_SIZE 4096

static inline uint64_t NbPageAlignUp (uint64_t ptr)
{
    // Dont align if already aligned
    if ((ptr & (NEXBOOT_CPU_PAGE_SIZE - 1)) == 0)
        return ptr;
    else
    {
        // Clear low 12 bits and them round to next page
        ptr &= ~(NEXBOOT_CPU_PAGE_SIZE - 1);
        ptr += NEXBOOT_CPU_PAGE_SIZE;
    }
    return ptr;
}

static inline uint64_t NbPageAlignDown (uint64_t ptr)
{
    return ptr & ~(NEXBOOT_CPU_PAGE_SIZE - 1);
}

/// Halts system
void NbCrash();

// Reads a CSR
#define NbCpuReadCsr(csr)                            \
    ({                                               \
        uint64_t __tmp = 0;                          \
        asm volatile("csrr %0, " csr : "=r"(__tmp)); \
        __tmp;                                       \
    })

#define NbCpuWriteCsr(csr, val) asm volatile("csrw " csr ", %0" : : "r"(val));

#define NB_KE_STACK_BASE 0xFFFFFFFF80000000

typedef uint64_t paddr_t;

// Launches kernel
void NbCpuLaunchKernel (uint64_t entry, uint64_t bootInf);

// Gets SATP
uint64_t NbCpuGetSatp();

#endif
