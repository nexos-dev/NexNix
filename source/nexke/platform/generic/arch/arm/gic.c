/*
    gic.c - contains Generic Interrupt Controller implementation
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

#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <nexke/platform/generic.h>
#include <string.h>

// TODO: SMP stuff, v3 and v4 support

// GIC distributor registers
#define PLT_GICD_CTRL         0
#define PLT_GICD_TYPE         0x4
#define PLT_GICD_IIDR         0x8
#define PLT_GICD_IGROUP_BASE  0x80
#define PLT_GICD_ISEN_BASE    0x100
#define PLT_GICD_ICEN_BASE    0x180
#define PLT_GICD_ISPEND_BASE  0x200
#define PLT_GICD_ICPEND_BASE  0x280
#define PLT_GICD_ISACT_BASE   0x300
#define PLT_GICD_ICACT_BASE   0x380
#define PLT_GICD_PRIO_BASE    0x400
#define PLT_GICD_ITARGET_BASE 0x800
#define PLT_GICD_ICFG_BASE    0xC00
#define PLT_GICD_SGI          0xF00
#define PLT_GICD_CSGI_BASE    0xF10
#define PLT_GICD_SSGI_BASE    0xF20
#define PLT_GICD_ICPIDR2      0xFE8

// GICD_CTRL
#define PLT_GICD_CTRL_EN (1 << 0)

// GICD_TYPE
#define PLT_GICD_LINES_MASK   0x1F
#define PLT_GICD_CPUNUM_SHIFT 5
#define PLT_GICD_CPUNUM_MASK  0x7
#define PLT_GICD_SECEXT       (1 << 10)

// Helpers for indexed registers
#define PLT_GIC_INT_REG(intNo)     ((intNo) / 8)
#define PLT_GIC_INT_BIT(intNo)     ((intNo) % 32)
#define PLT_GIC_INT_CFG(intNo)     ((intNo) / 4)
#define PLT_GIC_INT_CFG_BIT(intNo) ((intNo) % 8)

// Interrupt configuration bits
#define PLT_GICD_CFG_EDGE (1 << 1)
#define PLT_GICD_CFG_HIGH (1 << 0)

// ICPIDR2
#define PLT_GIC_ICPIDR_REV 4

// GIC CPU interface registers
#define PLT_GICC_CTRL       0
#define PLT_GICC_PMR        0x4
#define PLT_GICC_BPR        0x8
#define PLT_GICC_IAR        0xC
#define PLT_GICC_EOI        0x10
#define PLT_GICC_RPR        0x14
#define PLT_GICC_HPPIR      0x18
#define PLT_GICC_APR_BASE   0xD0
#define PLT_GICC_NSAPR_BASE 0xE0
#define PLT_GICC_IIDR       0xFC

// GICC_CTRL
#define PLT_GICC_ENABLE (1 << 0)

// GICC_IAR
#define PLT_GICC_INTID_MASK 0x3FF
#define PLT_GICC_CPU        10
#define PLT_GICC_CPU_MASK   0x7

// GICC_IIDR
#define PLT_GICC_REV      12
#define PLT_GICC_REV_MASK 0xF

#define PLT_GICC_MAX 8

extern PltHwIntCtrl_t gicIntCtrl;

typedef struct _pltgic
{
    void* giccBase;              // Base address of GICC
    int numGiccs;                // Number of GICCs
    void* gicdBase;              // Base of GICD
    int numLines;                // Number of GICD lines
    int basePrio;                // The highest priority we will use
    PltHwIntChain_t* lineMap;    // Map of all lines
    // uint8_t maskMap[8];          // Map of CPU number to CPU mask
} pltGic_t;

static pltGic_t gic = {0};

// Reads from GICC register
static inline uint32_t pltGiccReadReg (uint16_t reg)
{
    volatile uint32_t* regBase = (volatile uint32_t*) (gic.giccBase + reg);
    return *regBase;
}

// Writes to GICC register
static inline void pltGiccWriteReg (uint16_t reg, uint32_t val)
{
    volatile uint32_t* regBase = (volatile uint32_t*) (gic.giccBase + reg);
    *regBase = val;
}

// Reads GICD register
static inline uint32_t pltGicdReadReg (uint16_t reg)
{
    volatile uint32_t* regBase = (volatile uint32_t*) (gic.gicdBase + reg);
    return *regBase;
}

// Reads byte from GICD register
static inline uint8_t pltGicdReadReg8 (uint16_t reg)
{
    volatile uint8_t* regBase = (volatile uint8_t*) (gic.gicdBase + reg);
    return *regBase;
}

// Writes GICD register
static inline void pltGicdWriteReg (uint16_t reg, uint32_t val)
{
    volatile uint32_t* regBase = (volatile uint32_t*) (gic.gicdBase + reg);
    *regBase = val;
}

// Writes byte to GICD register
static inline void pltGicdWriteReg8 (uint16_t reg, uint32_t val)
{
    volatile uint8_t* regBase = (volatile uint8_t*) (gic.gicdBase + reg);
    *regBase = val;
}

// Setsup GICD for specified interrupt object
static inline void pltGicdSetupInterrupt (NkHwInterrupt_t* intObj)
{
    uint32_t gsi = intObj->gsi;
    // Disable it first
    pltGicdWriteReg (PLT_GICD_ICEN_BASE + PLT_GIC_INT_REG (gsi), 1 << PLT_GIC_INT_BIT (gsi));
    // Set target priority
    pltGicdWriteReg8 (PLT_GICD_PRIO_BASE + gsi, gic.basePrio - intObj->ipl);
    // Set target CPU
    pltGicdWriteReg8 (PLT_GICD_ITARGET_BASE + gsi, 1 << PltGetPlatform()->bsp->id);
    // Create mask
    int cfgMask = (intObj->mode == PLT_MODE_EDGE) ? PLT_GICD_CFG_EDGE : 0;
    cfgMask |= (intObj->flags & PLT_HWINT_ACTIVE_LOW) ? 0 : PLT_GICD_CFG_HIGH;
    cfgMask <<= PLT_GIC_INT_CFG_BIT (gsi);
    // Read current mask
    uint32_t icfg = pltGicdReadReg (PLT_GICD_ICFG_BASE + PLT_GIC_INT_CFG (gsi));
    // Add it in
    icfg &= ~(cfgMask);
    icfg |= cfgMask;
    // Write it
    pltGicdWriteReg (PLT_GICD_ICFG_BASE + PLT_GIC_INT_CFG (gsi), icfg);
    // Enable it again
    pltGicdWriteReg (PLT_GICD_ISEN_BASE + PLT_GIC_INT_REG (gsi), 1 << PLT_GIC_INT_BIT (gsi));
}

// Interface functions
static bool PltGicBeginInterrupt (NkCcb_t* ccb, int vector)
{
    if (ccb->archCcb.spuriousInt)
    {
        ccb->archCcb.spuriousInt = false;
        return false;    // Spurious interrupt is pending
    }
    return true;
}

static void PltGicEndInterrupt (NkCcb_t* ccb, int vector)
{
    // Write EOI to EOIR
    pltGiccWriteReg (PLT_GICC_EOI, ccb->archCcb.savedIar);
}

static void PltGicDisableInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    // Get line number
    uint32_t gsi = intObj->gsi;
    // Disable it
    pltGicdWriteReg (PLT_GICD_ICEN_BASE + PLT_GIC_INT_REG (gsi), 1 << PLT_GIC_INT_BIT (gsi));
}

static void PltGicEnableInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    // Get line number
    uint32_t gsi = intObj->gsi;
    // Enable it
    pltGicdWriteReg (PLT_GICD_ISEN_BASE + PLT_GIC_INT_REG (gsi), 1 << PLT_GIC_INT_BIT (gsi));
}

static void PltGicSetIpl (NkCcb_t* ccb, ipl_t ipl)
{
    // Write to the PMR
    pltGiccWriteReg (PLT_GICC_PMR, gic.basePrio - ipl);
}

static int PltGicConnectInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    assert (intObj->gsi >= 32);
    PltHwIntChain_t* chain = &gic.lineMap[intObj->gsi];
    NkSpinLock (&chain->lock);
    // Check if we neec to chain it
    if (NkListFront (&chain->list))
    {
        NkHwInterrupt_t* chainFront =
            LINK_CONTAINER (NkListFront (&chain->list), NkHwInterrupt_t, link);
        // Interrupt is in use, make sure this will work
        if (intObj->flags & PLT_HWINT_NON_CHAINABLE || !PltAreIntsCompatible (intObj, chainFront) ||
            intObj->mode == PLT_MODE_EDGE)
        {
            return -1;
        }
        // If interrupt is happy with any IPL, this will work
        // If FORCE_IPL is set and the IPL of the interrupt and chain aren't equal
        // we need to remap the chain
        // Unless this chain is not remappable
        if (intObj->flags & PLT_HWINT_FORCE_IPL)
        {
            // Remap if we can
            if (chain->noRemap)
            {
                NkSpinUnlock (&chain->lock);
                return -1;    // Can't do it
            }
            // Remap IPL
            if (!PltRemapInterrupt (PltGetInterrupt (chainFront->vector),
                                    chainFront->vector,
                                    intObj->ipl))
            {
                NkSpinUnlock (&chain->lock);
                return -1;    // Can't do it
            }
        }
        else
            intObj->ipl = chainFront->ipl;    // Set IPL of this interrupt
    }
    // Set vector
    intObj->vector = intObj->gsi + CPU_BASE_HWINT;
    // Setup the interrupt now
    pltGicdSetupInterrupt (intObj);
    NkSpinUnlock (&chain->lock);
    return intObj->vector;
}

static void PltGicDisconnectInterrupt (NkCcb_t* ccb, NkHwInterrupt_t* intObj)
{
    PltHwIntChain_t* chain = &gic.lineMap[intObj->gsi];
    if (chain->chainLen == 0)
    {
        // Disable it
        uint32_t gsi = intObj->gsi;
        pltGicdWriteReg (PLT_GICD_ICEN_BASE + PLT_GIC_INT_REG (gsi), 1 << PLT_GIC_INT_BIT (gsi));
    }
}

static int PltGicGetVector (NkCcb_t* ccb)
{
    // Acknowledge the interrupt
    uint32_t iar = pltGiccReadReg (PLT_GICC_IAR);
    return CPU_BASE_HWINT + (iar & PLT_GICC_INTID_MASK);
}

// GIC structure
PltHwIntCtrl_t gicIntCtrl = {.type = PLT_HWINT_GIC,
                             .beginInterrupt = PltGicBeginInterrupt,
                             .endInterrupt = PltGicEndInterrupt,
                             .disableInterrupt = PltGicDisableInterrupt,
                             .enableInterrupt = PltGicEnableInterrupt,
                             .setIpl = PltGicSetIpl,
                             .connectInterrupt = PltGicConnectInterrupt,
                             .disconnectInterrupt = PltGicDisconnectInterrupt,
                             .getVector = PltGicGetVector};

// Initializes GICD
static bool pltGicdInit()
{
    // Grab first int controller from platform, we only support one GICD
    NkLink_t* gicdLink = NkListFront (&PltGetPlatform()->intCtrls);
    if (!gicdLink)
        return false;
    PltIntCtrl_t* ctrl = LINK_CONTAINER (gicdLink, PltIntCtrl_t, link);
    if (ctrl->type != PLT_INTCTRL_GIC)
        return false;
    // Map it
    gic.gicdBase = MmAllocKvMmio ((paddr_t) ctrl->addr,
                                  1,
                                  MUL_PAGE_DEV | MUL_PAGE_R | MUL_PAGE_RW | MUL_PAGE_KE);
    uint32_t type = pltGicdReadReg (PLT_GICD_TYPE);
    gic.numLines = type & PLT_GICD_LINES_MASK;
    // Allocate line map
    size_t mapSz = (gic.numLines + 32) * sizeof (PltHwIntChain_t);
    gic.lineMap = (PltHwIntChain_t*) MmAllocKvRegion (CpuPageAlignUp (mapSz) / NEXKE_CPU_PAGESZ,
                                                      MM_KV_NO_DEMAND);
    assert (gic.lineMap);
    memset (gic.lineMap, 0, mapSz);
    // Disable it for now
    pltGicdWriteReg (PLT_GICD_CTRL, 0);
    // Disable all interrupts on it
    for (int i = 0; i < gic.numLines; ++i)
        pltGicdWriteReg (PLT_GICD_ICEN_BASE + PLT_GIC_INT_REG (i), 0xFFFFFFFF);
    // Enable
    pltGicdWriteReg (PLT_GICD_CTRL, PLT_GICD_CTRL_EN);
    return true;
}

// Initializes GICC
static bool pltGiccInit()
{
    if (gic.numGiccs == PLT_GICC_MAX)
    {
        NkLogWarning ("nexke: too many GICCs found\n");
        return false;
    }
    // Grab the first CPU from the platform and use it's base address
    // The GIC spec strongly recommends all GICCs have the same local base and since we don't
    // know which CPU we are running on we have to hope that is true
    NkLink_t* cpuLink = NkListFront (&PltGetPlatform()->cpus);
    if (!cpuLink)
        return false;
    PltCpu_t* cpu = LINK_CONTAINER (cpuLink, PltCpu_t, link);
    if (cpu->type != PLT_CPU_GIC)
        return false;
    // Allocate GIC virtual base if we haven't already
    if (!gic.giccBase)
    {
        gic.giccBase =
            MmAllocKvMmio (cpu->addr, 1, MUL_PAGE_DEV | MUL_PAGE_R | MUL_PAGE_RW | MUL_PAGE_KE);
    }
    // Get self ID
    int selfId = 0;
    for (int i = 0; i < 32; ++i)
    {
        uint8_t mask = pltGicdReadReg8 (PLT_GICD_ITARGET_BASE + i);
        if (mask)
        {
            selfId = NkGetHighBit (mask);
            break;
        }
    }
    // Figure out which one is BSP
    NkLink_t* iter = NkListFront (&PltGetPlatform()->cpus);
    while (iter)
    {
        PltCpu_t* curCpu = LINK_CONTAINER (iter, PltCpu_t, link);
        if (curCpu->id == selfId)
        {
            // Set this as BSP
            NkLogDebug ("nexke: found BSP at CPU %d\n", curCpu->id);
            PltGetPlatform()->bsp = curCpu;
            break;
        }
        iter = NkListIterate (iter);
    }
    // Get max priority
    pltGicdWriteReg8 (PLT_GICD_PRIO_BASE + 32, 0xFF);
    gic.basePrio = pltGicdReadReg8 (PLT_GICD_PRIO_BASE + 32);
    // Set priority
    pltGiccWriteReg (PLT_GICC_PMR, 0xFF);
    // Enable
    pltGiccWriteReg (PLT_GICC_CTRL, PLT_GICC_ENABLE);
    return true;
}

// Initializes GIC driver
PltHwIntCtrl_t* PltGicInit()
{
    // Initialize GICDs
    if (!pltGicdInit())
        return NULL;
    // Initialize GICC
    if (!pltGiccInit())
        return NULL;
    return &gicIntCtrl;
}
