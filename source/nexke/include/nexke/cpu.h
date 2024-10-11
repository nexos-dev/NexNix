/*
    cpu.h - contains CPU related kernel functions
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

#ifndef _CPU_H
#define _CPU_H

// Include arch header. This makes use of computed includes
#include NEXKE_ARCH_HEADER
#include <nexke/list.h>
#include <nexke/types.h>

// CCB structure (aka CPU control block)
// This is the core data structure for the CPU, and hence, the kernel
typedef struct _nkccb
{
    // General CPU info
    int cpuArch;      // CPU architecture
    int cpuFamily;    // Architecture family
    int sysBoard;     // System hardware / SOC type
    char sysName[64];
    NkArchCcb_t archCcb;    // Architecture dependent part of CCB
    // Interrupt handling data
    ipl_t curIpl;          // IPL system is running at
    int spuriousInts;      // Number of spurious interrupts to occur
    long long intCount;    // Interrupt count
    bool intActive;        // Wheter an interrupt is active on this CPU
                           // This flags is only set during hardware interrupt processing
    // Timer related data
    NkList_t timeEvents;    // Linked list of time events waiting to occur
} NkCcb_t;

// Defined CPU architectures
#define NEXKE_CPU_I386   1
#define NEXKE_CPU_X86_64 2

// Defined CPU familys
#define NEXKE_CPU_FAMILY_X86 1

// Defined boards
#define NEXKE_BOARD_PC 1

// Initializes CPU control block
void CpuInitCcb();

// Registers exception handlers
void CpuRegisterExecs();

// Returns CCB to caller
NkCcb_t* CpuGetCcb();

// Print CPU features
void CpuPrintFeatures();

// Prints debug info about crash
void CpuPrintDebug (CpuIntContext_t* context);

// Holds interrupts
void CpuHoldInts();

// Lets interrupts occur
void CpuUnholdInts();

// Disables interrupts
void CpuDisable();

// Enables interrupts
void CpuEnable();

// Asserts that we are not in an interrupt
#define CPU_ASSERT_NOT_INT()    \
    if (CpuGetCcb()->intActive) \
        NkPanic ("nexke: interrupt check failed\n");

#define CPU_IS_INT() (CpuGetCcb()->intActive)

// CPU exception info
typedef struct _execinf
{
    const char* name;    // Exception name
} CpuExecInf_t;

// Gets exception diagnostic info from CPU
void CpuGetExecInf (CpuExecInf_t* out, NkInterrupt_t* intObj, CpuIntContext_t* ctx);

// Page aligning inlines
static inline uintptr_t CpuPageAlignUp (uintptr_t ptr)
{
    // Dont align if already aligned
    if ((ptr & (NEXKE_CPU_PAGESZ - 1)) == 0)
        return ptr;
    else
    {
        // Clear low 12 bits and them round to next page
        ptr &= ~(NEXKE_CPU_PAGESZ - 1);
        ptr += NEXKE_CPU_PAGESZ;
    }
    return ptr;
}

static inline uintptr_t CpuPageAlignDown (uintptr_t ptr)
{
    return ptr & ~(NEXKE_CPU_PAGESZ - 1);
}

#endif
