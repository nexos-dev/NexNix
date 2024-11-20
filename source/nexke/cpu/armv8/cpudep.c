/*
    cpudep.c - contains CPU dependent part of nexke
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

#include <nexke/cpu.h>
#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <string.h>

// Globals

// The system's CCB. A very important data structure that contains the kernel's
// deepest bowels
static NkCcb_t ccb = {0, .preemptDisable = 1};    // The CCB

bool ccbInit = false;

// Checks a feature
#define CPU_CHECK_FEATURE(reg, shift, val, feature)    \
    if ((((reg) >> (shift)) & CPU_FEAT_MASK) == (val)) \
        ccb.archCcb.features |= (feature);

// Prepares CCB data structure. This is the first thing called during boot
void CpuInitCcb()
{
    // Grab boot info
    NexNixBoot_t* bootInfo = NkGetBootArgs();
    // Set up basic fields
    ccb.self = &ccb;
    ccb.cpuArch = NEXKE_CPU_ARMV8;
    ccb.cpuFamily = NEXKE_CPU_FAMILY_ARM;
#ifdef NEXNIX_BOARD_GENERIC
    ccb.sysBoard = NEXKE_BOARD_GENERIC;
#else
#error Unrecognized board
#endif
    ccb.archCcb.intHold = true;    // Keep interrupts held at first
    ccb.archCcb.intReq = true;
    strcpy (ccb.sysName, bootInfo->sysName);
    // Perform feature detection
    uint64_t isar0 = CpuReadSpr ("ID_AA64ISAR0_EL1");
    // Check features
    CPU_CHECK_FEATURE (isar0, CPU_ISAR0_RNDR, 1, CPU_FEATURE_RNG);
    CPU_CHECK_FEATURE (isar0, CPU_ISAR0_ATOMIC, 2, CPU_FEATURE_ATOMIC);
    CPU_CHECK_FEATURE (isar0, CPU_ISAR0_ATOMIC, 3, CPU_FEATURE_ATOMIC);
    CPU_CHECK_FEATURE (isar0, CPU_ISAR0_CRC32, 1, CPU_FEATURE_CRC32);
    uint64_t isar1 = CpuReadSpr ("ID_AA64ISAR1_EL1");
    CPU_CHECK_FEATURE (isar1, CPU_ISAR1_XS, 1, CPU_FEATURE_XS);
    uint64_t mmfr0 = CpuReadSpr ("ID_AA64MMFR0_EL1");
    CPU_CHECK_FEATURE (mmfr0, CPU_MMFR0_EXS, 1, CPU_FEATURE_EXS);
    CPU_CHECK_FEATURE (mmfr0, CPU_MMFR0_TGRAN4, 1, CPU_FEATURE_TGRAN4);
    CPU_CHECK_FEATURE (mmfr0, CPU_MMFR0_TGRAN4, 0, CPU_FEATURE_TGRAN4);
    CPU_CHECK_FEATURE (mmfr0, CPU_MMFR0_TGRAN64, 1, CPU_FEATURE_TGRAN64);
    CPU_CHECK_FEATURE (mmfr0, CPU_MMFR0_TGRAN64, 0, CPU_FEATURE_TGRAN64);
    CPU_CHECK_FEATURE (mmfr0, CPU_MMFR0_TGRAN16, 1, CPU_FEATURE_TGRAN16);
    CPU_CHECK_FEATURE (mmfr0, CPU_MMFR0_TGRAN16, 0, CPU_FEATURE_TGRAN16);
    CPU_CHECK_FEATURE (mmfr0, CPU_MMFR0_BIGENDEL0, 1, CPU_FEATURE_MIX_ENDIAN_EL0);
    CPU_CHECK_FEATURE (mmfr0, CPU_MMFR0_SNSMEM, 1, CPU_FEATURE_SECURE_MEM);
    CPU_CHECK_FEATURE (mmfr0, CPU_MMFR0_BIGEND, 1, CPU_FEATURE_MIX_ENDIAN);
    // Set ASID bits and PA bits
    int asidBits = (mmfr0 >> CPU_MMFR0_ASIDBITS) & CPU_FEAT_MASK;
    if (asidBits == 0)
        ccb.archCcb.asidBits = 8;
    else
        ccb.archCcb.asidBits = 16;
    // Get PA bits
    int paBits = (mmfr0 >> CPU_MMFR0_PABITS) & CPU_FEAT_MASK;
    if (paBits == 0)
        ccb.archCcb.paBits = 32;
    else if (paBits == 1)
        ccb.archCcb.paBits = 36;
    else if (paBits == 2)
        ccb.archCcb.paBits = 40;
    else if (paBits == 3)
        ccb.archCcb.paBits = 42;
    else if (paBits == 4)
        ccb.archCcb.paBits = 44;
    else if (paBits == 5)
        ccb.archCcb.paBits = 48;
    else if (paBits == 6)
        ccb.archCcb.paBits = 52;
    else if (paBits == 7)
        ccb.archCcb.paBits = 56;
    uint64_t mmfr2 = CpuReadSpr ("ID_AA64MMFR2_EL1");
    CPU_CHECK_FEATURE (mmfr2, CPU_MMFR2_E0PD, 1, CPU_FEATURE_E0PD);
    CPU_CHECK_FEATURE (mmfr2, CPU_MMFR2_CNP, 1, CPU_FEATURE_CNP);
    // Get supported VA bits
    int vaBits = (mmfr2 >> CPU_MMFR2_VABITS) & CPU_FEAT_MASK;
    if (vaBits == 0)
        ccb.archCcb.vaBits = 48;
    else if (vaBits == 1)
        ccb.archCcb.vaBits = 52;
    else if (vaBits == 2)
        ccb.archCcb.vaBits = 56;
    else
        ccb.archCcb.vaBits = 48;    // Lower common denominator, play it safe
    uint64_t pfr0 = CpuReadSpr ("ID_AA64PFR0_EL1");
    CPU_CHECK_FEATURE (pfr0, CPU_PFR0_GIC, 3, CPU_FEATURE_GIC41);
    CPU_CHECK_FEATURE (pfr0, CPU_PFR0_GIC, 1, CPU_FEATURE_GIC3_4);
    CPU_CHECK_FEATURE (pfr0, CPU_PFR0_EL0_AA32, 2, CPU_FEATURE_EL0_AA32);
    uint64_t pfr1 = CpuReadSpr ("ID_AA64PFR1_EL1");
    CPU_CHECK_FEATURE (pfr1, CPU_PFR1_NMI, 1, CPU_FEATURE_NMI);
    // Setup SCTLR
    uint64_t sctlr = CpuReadSpr ("SCTLR_EL1");
    sctlr &= ~(CPU_SCTLR_EL0_ENDIAN | CPU_SCTLR_UMA | CPU_SCTLR_WFI_EL0);
    // Set bits
    sctlr |= (CPU_SCTLR_MMU_EN | CPU_SCTLR_CACHE | CPU_SCTLR_SP_ALIGN | CPU_SCTLR_SP0_ALIGN |
              CPU_SCTLR_INST_CACHE);
    if (CpuGetFeatures() & CPU_FEATURE_NMI)
        sctlr |= (uint64_t) CPU_SCTLR_NMI;
    CpuWriteSpr ("SCTLR_EL1", sctlr);
    // Setup interrupts
    CpuDisable();
    CpuWriteSpr ("VBAR_EL1", CpuVectorTable);
    // Save CCB to TPIDR
    CpuWriteSpr ("TPIDR_EL1", (uint64_t) &ccb);
    ccbInit = true;
}

// Returns CCB to caller
NkCcb_t* CpuRealCcb()
{
    return &ccb;
}

// Gets feature flags
uint64_t CpuGetFeatures()
{
    return ccb.archCcb.features;
}

static const char* cpuFeatures[] = {"RNG",
                                    "ATOMIC",
                                    "CRC32",
                                    "XS",
                                    "EXS",
                                    "TGRAN4",
                                    "TGRAN64",
                                    "TGRAN16",
                                    "MIX_ENDIAN_EL0",
                                    "MIX_ENDIAN",
                                    "SECURE_MEM",
                                    "E0PD",
                                    "CNP",
                                    "GIC3_4",
                                    "GIC4.1",
                                    "EL0_AA32",
                                    "NMI"};

// Print CPU features
void CpuPrintFeatures()
{
    // Log out supported features
    NkLogInfo ("nexke: detected CPU features: ");
    for (int i = 0; i < 64; ++i)
    {
        if (CpuGetCcb()->archCcb.features & (1ULL << i))
            NkLogInfo ("%s ", cpuFeatures[i]);
    }
    NkLogInfo ("\n");
}

// Allocates a CPU context and intializes it
CpuContext_t* CpuAllocContext (uintptr_t entry)
{
}

// Destroys a context
void CpuDestroyContext (CpuContext_t* context)
{
}
