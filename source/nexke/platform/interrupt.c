/*
    interrupt.c - contains interrupt dispatcher
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

#include <assert.h>
#include <nexke/cpu.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Interrupt table
static NkInterrupt_t* nkIntTable[NK_MAX_INTS] = {NULL};
static spinlock_t nkIntTabLock = 0;

// Slab cache
static SlabCache_t* nkIntCache = NULL;
static SlabCache_t* nkHwIntCache = NULL;

// Platform static pointer
static NkPlatform_t* platform = NULL;

// Chain for all internal interrupts
static PltHwIntChain_t internalChain = {0};

// Chain helpers

static inline PltHwIntChain_t* pltGetChain (uint32_t gsi)
{
    if (gsi == PLT_GSI_INTERNAL)
        return &internalChain;
    return &platform->intCtrl->lineMap[gsi];
}

static inline size_t pltGetLineMapSize()
{
    return platform->intCtrl->numLines;
}

static inline void pltInitChain (PltHwIntChain_t* chain)
{
    NkListInit (&chain->list);
    chain->chainLen = 0;
    chain->noRemap = false;
}

// Interrupt allocation
static inline NkInterrupt_t* pltAllocInterrupt (int vector, int type)
{
    NkSpinLock (&nkIntTabLock);
    // Ensure vector is free
    if (nkIntTable[vector])
    {
        NkSpinUnlock (&nkIntTabLock);
        return NULL;    // Interrupt is in use
    }
    NkInterrupt_t* obj = (NkInterrupt_t*) MmCacheAlloc (nkIntCache);
    if (!obj)
        NkPanic ("nexke: out of memory");
    memset (obj, 0, sizeof (NkInterrupt_t));
    // Initialize object
    obj->callCount = 0;
    obj->type = type;
    obj->vector = vector;
    // Insert in table
    nkIntTable[vector] = obj;
    NkSpinUnlock (&nkIntTabLock);
    return obj;
}

// Increases call count
static inline void pltIncCallCount (NkInterrupt_t* obj)
{
    NkSpinLock (&obj->lock);
    ++obj->callCount;
    NkSpinUnlock (&obj->lock);
}

// Adds interrupt to chain
static inline void pltChainInterrupt (NkInterrupt_t* obj, NkHwInterrupt_t* hwInt)
{
    assert (obj->type == PLT_INT_HWINT);
    assert (hwInt->gsi == PLT_GSI_INTERNAL || hwInt->gsi < pltGetLineMapSize());
    PltHwIntChain_t* chain = pltGetChain (hwInt->gsi);
    if (!chain->chainLen)
        pltInitChain (chain);
    // Link it
    NkListAddFront (&chain->list, &hwInt->link);
    ++chain->chainLen;
    // Check if we need to mark it as chained
    if (chain->chainLen > 1)
    {
        hwInt->flags |= PLT_HWINT_CHAINED;
        if (chain->chainLen == 2)
        {
            // The chain just started so we need to set the bit in current chained item
            NkHwInterrupt_t* hwInt2 = LINK_CONTAINER (hwInt->link.next, NkHwInterrupt_t, link);
            hwInt2->flags |= PLT_HWINT_CHAINED;
        }
    }
}

// Removes interrupt from chain
static inline void pltUnchainInterrupt (NkInterrupt_t* obj, NkHwInterrupt_t* hwInt)
{
    assert (obj->type == PLT_INT_HWINT);
    PltHwIntChain_t* chain = pltGetChain (hwInt->gsi);
    assert (hwInt->gsi == PLT_GSI_INTERNAL || hwInt->gsi < pltGetLineMapSize());
    // Unlink it
    NkListRemove (&chain->list, &hwInt->link);
    --chain->chainLen;
    if (chain->chainLen == 1)
    {
        // Unmark it as chained
        NkHwInterrupt_t* headInt =
            LINK_CONTAINER (NkListFront (&chain->list), NkHwInterrupt_t, link);
        headInt->flags &= ~(PLT_HWINT_CHAINED);
    }
}

// Checks if two hardware interrupts are compatible
bool PltAreIntsCompatible (NkHwInterrupt_t* int1, NkHwInterrupt_t* int2)
{
    if (int1->mode != int2->mode ||
        (int1->flags & PLT_HWINT_ACTIVE_LOW != int2->flags & PLT_HWINT_ACTIVE_LOW))
    {
        return false;    // Can't do it
    }
    return true;
}

// Retrieves interrupt obejct from table
NkInterrupt_t* PltGetInterrupt (int vector)
{
    assert (vector < NK_MAX_INTS);
    NkSpinLock (&nkIntTabLock);
    NkInterrupt_t* intObj = nkIntTable[vector];
    NkSpinUnlock (&nkIntTabLock);
    return intObj;
}

// Installs an exception handler
NkInterrupt_t* PltInstallExec (int vector, PltIntHandler hndlr)
{
    assert (vector < NK_MAX_INTS);
    if (vector > CPU_BASE_HWINT)
        return NULL;    // Can't cross into hardware vectors
    CpuDisable();
    NkInterrupt_t* obj = pltAllocInterrupt (vector, PLT_INT_EXEC);
    if (!obj)
    {
        CpuEnable();
        return NULL;
    }
    NkSpinLock (&obj->lock);
    obj->handler = hndlr;
    NkSpinUnlock (&obj->lock);
    CpuEnable();
    return obj;
}

// Installs a service handler
NkInterrupt_t* PltInstallSvc (int vector, PltIntHandler hndlr)
{
    assert (vector < NK_MAX_INTS);
    if (vector > CPU_BASE_HWINT)
        return NULL;    // Can't cross into hardware vectors
    CpuDisable();
    NkInterrupt_t* obj = pltAllocInterrupt (vector, PLT_INT_SVC);
    if (!obj)
    {
        CpuEnable();
        return NULL;
    }
    NkSpinLock (&obj->lock);
    obj->handler = hndlr;
    NkSpinUnlock (&obj->lock);
    CpuEnable();
    return obj;
}

// Initializes a hardware interrupt
void PltInitInterrupt (NkHwInterrupt_t* hwInt,
                       PltIntHandler handler,
                       uint32_t gsi,
                       ipl_t ipl,
                       int mode,
                       int flags)
{
    hwInt->handler = handler;
    hwInt->gsi = gsi;
    hwInt->ipl = ipl;
    if (hwInt->ipl == 0)
        ++hwInt->ipl;    // Cant have an IPL of 0
    hwInt->mode = mode;
    hwInt->flags = flags;
}

// Initializes a  internal interrupt
void PltInitInternalInt (NkHwInterrupt_t* hwInt,
                         PltIntHandler handler,
                         int vector,
                         ipl_t ipl,
                         int mode,
                         int flags)
{
    hwInt->handler = handler;
    hwInt->gsi = PLT_GSI_INTERNAL;
    hwInt->vector = vector;
    hwInt->ipl = ipl;
    if (hwInt->ipl == 0)
        ++hwInt->ipl;    // Cant have an IPL of 0
    hwInt->mode = mode;
    hwInt->flags = flags | PLT_HWINT_INTERNAL;
}

// Installs a hardware interrupt
NkInterrupt_t* PltConnectInterrupt (NkHwInterrupt_t* hwInt)
{
    // Validate interrupt
    if (hwInt->ipl > PLT_IPL_TIMER)
        return NULL;
    CpuDisable();
    // Connect the interrupt first if this is not an internal interrupt
    PltHwIntChain_t* chain = pltGetChain (hwInt->gsi);
    NkSpinLock (&chain->lock);
    int vector = 0;
    if (!(hwInt->flags & PLT_HWINT_INTERNAL))
        vector = platform->intCtrl->connectInterrupt (CpuGetCcb(), hwInt);
    else
        vector = hwInt->vector;
    // Check if interrupt is installed
    NkInterrupt_t* obj = PltGetInterrupt (vector);
    if (obj)
    {
        // Make sure this is allowed
        if (hwInt->flags & PLT_HWINT_NON_CHAINABLE || !(hwInt->flags & PLT_HWINT_INTERNAL))
            return NULL;
        pltChainInterrupt (obj, hwInt);
    }
    else
    {
        // Allocate a new interrupt
        obj = pltAllocInterrupt (vector, PLT_INT_HWINT);
        // Set chain
        NkSpinLock (&obj->lock);
        obj->intChain = chain;
        NkSpinUnlock (&obj->lock);
        // Start chain
        pltChainInterrupt (obj, hwInt);
        // Enable it if not an internally managed interrupt
        if (!(hwInt->flags & PLT_HWINT_INTERNAL))
            platform->intCtrl->enableInterrupt (CpuGetCcb(), hwInt);
    }
    NkSpinUnlock (&chain->lock);
    CpuEnable();
    return obj;
}

// Disconnects interrupt from hardware controller
void PltDisconnectInterrupt (NkHwInterrupt_t* hwInt)
{
    // Unchain and then disconnect it
    CpuDisable();
    PltHwIntChain_t* chain = pltGetChain (hwInt->gsi);
    NkSpinLock (&chain->lock);
    pltUnchainInterrupt (PltGetInterrupt (hwInt->vector), hwInt);
    // NOTE: if interrupt is not chained, disconnect will disable it for us
    platform->intCtrl->disconnectInterrupt (CpuGetCcb(), hwInt);
    NkSpinUnlock (&chain->lock);
    CpuEnable();
}

// Remaps hardware interrupts on specified object to a new vector and IPL
// Requires input to be a hardware interrupt object, and returns the new interrupt
// Called with interrupts disabled and chain locked
NkInterrupt_t* PltRemapInterrupt (NkInterrupt_t* oldInt, int newVector, ipl_t newIpl)
{
    assert (newVector < NK_MAX_INTS);
    assert (oldInt->type == PLT_INT_HWINT);
    // Allocate the new vector if these is a new one
    NkInterrupt_t* newInt = NULL;
    if (oldInt->vector != newVector)
    {
        newInt = pltAllocInterrupt (newVector, PLT_INT_HWINT);
        if (!newInt)
            return NULL;
        NkSpinLock (&newInt->lock);
        // Move the chain
        newInt->intChain = oldInt->intChain;
        NkSpinUnlock (&newInt->lock);
        // Uninstall the old interrupt
        PltUninstallInterrupt (oldInt);
    }
    else
        newInt = oldInt;
    // Change vector and IPL on all the interrupts
    NkList_t* chain = &newInt->intChain->list;
    NkLink_t* iter = NkListFront (chain);
    while (iter)
    {
        NkHwInterrupt_t* curInt = LINK_CONTAINER (iter, NkHwInterrupt_t, link);
        curInt->vector = newVector;
        curInt->ipl = newIpl;
        iter = NkListIterate (chain, iter);
    }
    return newInt;
}

// Enables an interrupt
void PltEnableInterrupt (NkHwInterrupt_t* hwInt)
{
    CpuDisable();
    PltHwIntChain_t* chain = pltGetChain (hwInt->gsi);
    NkSpinLock (&chain->lock);
    platform->intCtrl->enableInterrupt (CpuGetCcb(), hwInt);
    NkSpinUnlock (&chain->lock);
    CpuEnable();
}

// Disables an interrupt
void PltDisableInterrupt (NkHwInterrupt_t* hwInt)
{
    CpuDisable();
    PltHwIntChain_t* chain = pltGetChain (hwInt->gsi);
    NkSpinLock (&chain->lock);
    platform->intCtrl->disableInterrupt (CpuGetCcb(), hwInt);
    NkSpinUnlock (&chain->lock);
    CpuEnable();
}

// Uninstalls an interrupt handler
void PltUninstallInterrupt (NkInterrupt_t* intObj)
{
    CpuDisable();
    NkSpinLock (&nkIntTabLock);
    if (!nkIntTable[intObj->vector])
        NkPanic ("nexke: can't uninstall non-existant interrupt");
    nkIntTable[intObj->vector] = NULL;
    NkSpinUnlock (&nkIntTabLock);
    CpuEnable();
    MmCacheFree (nkIntCache, intObj);
}

// Initializes interrupt system
void PltInitInterrupts()
{
    // Store platform pointer
    platform = PltGetPlatform();
    // Create cache
    nkIntCache = MmCacheCreate (sizeof (NkInterrupt_t), "NkInterrupt_t", 0, 0);
    nkHwIntCache = MmCacheCreate (sizeof (NkHwInterrupt_t), "NkHwInterrupt_t", 0, 0);
    // Register CPU exception handlers
    CpuRegisterExecs();
}

// Raises IPL to specified level
ipl_t PltRaiseIpl (ipl_t newIpl)
{
    CpuDisable();    // For safety
    NkCcb_t* ccb = CpuGetCcb();
    if (ccb->curIpl > newIpl)
        NkPanic ("nexke: invalid IPL to raise to");
    ipl_t oldIpl = ccb->curIpl;
    ccb->curIpl = newIpl;    // Set IPL
    // Re-enable if needed
    if (newIpl != PLT_IPL_HIGH)
    {
        platform->intCtrl->setIpl (ccb, newIpl);    // Do it on the hardware side
        CpuEnable();
    }
    return oldIpl;
}

// Lowers IPL back to level
void PltLowerIpl (ipl_t oldIpl)
{
    CpuDisable();    // For safety
    NkCcb_t* ccb = CpuGetCcb();
    if (ccb->curIpl < oldIpl)
        NkPanic ("nexke: Invalid IPL to lower to");
    ccb->curIpl = oldIpl;    // Restore it
    // Re-enable if needed
    if (oldIpl != PLT_IPL_HIGH)
    {
        platform->intCtrl->setIpl (ccb, oldIpl);    // Do it on the hardware side
        CpuEnable();
    }
}

// Called when a trap goes bad and the system needs to crash
void PltBadTrap (CpuIntContext_t* context, const char* msg, ...)
{
    va_list ap;
    va_start (ap, msg);
    // Print the info
    NkLogMessage ("nexke: bad trap: ", NK_LOGLEVEL_EMERGENCY, ap);
    NkLogMessage (msg, NK_LOGLEVEL_EMERGENCY, ap);
    NkLogMessage ("\n", NK_LOGLEVEL_EMERGENCY, ap);
// If we are in a debug build, print debugging info
#ifndef NDEBUG
    CpuPrintDebug (context);
#endif
    va_end (ap);
    // Crash the system
    CpuCrash();
}

// Exception dispatcher. Called when first level handling fails
void PltExecDispatch (NkInterrupt_t* intObj, CpuIntContext_t* context)
{
    // For now this is simple. We just always crash
    CpuExecInf_t execInf = {0};
    CpuGetExecInf (&execInf, intObj, context);
    PltBadTrap (context, "%s", execInf.name);
}

// Trap dispatcher
void PltTrapDispatch (CpuIntContext_t* context)
{
    NkCcb_t* ccb = CpuGetCcb();
    ++ccb->intCount;
    // Grab the interrupt object
    NkInterrupt_t* intObj = PltGetInterrupt (CPU_CTX_INTNUM (context));
    if (!intObj)
    {
        // Unhandled interrupt, that's a bad trap
        PltBadTrap (context, "unhandled interrupt %#X", CPU_CTX_INTNUM (context));
    }
    NkSpinLock (&intObj->lock);
    ++intObj->callCount;
    NkSpinUnlock (&intObj->lock);
    // Now we need to determine what kind of trap this is. There are 3 possibilities
    // If this is an exception, first we call the registered handler. If the handler fails to handle
    // the int, then we call PltExecDispatch to perform default processing
    // If this is a service, we call the handler and then we are finished
    // If this is a hardware interrupt, our job is more complicated.
    // First we must set the IPL to the level of the interrupt, and then we enable interrupts
    // from the hardware. Then we check we must check if it's spurious. If not, then we call the
    // handler function before finally telling the interrupt hardware to end the interrupt
    if (intObj->type == PLT_INT_EXEC)
    {
        // First call the handler (if one exists) and see if it resolves it
        bool resolved = false;
        if (intObj->handler)
            resolved = intObj->handler (intObj, context);
        if (!resolved)
        {
            // This means this exception is in error. We must call the exception dispatcher
            PltExecDispatch (intObj, context);
        }
    }
    else if (intObj->type == PLT_INT_SVC)
    {
        intObj->handler (intObj, context);    // This will never fail
    }
    else if (intObj->type == PLT_INT_HWINT)
    {
        // Disable preemption if needed
        bool preemptSet = ccb->preemptDisable;
        if (!preemptSet)
            TskDisablePreempt();
        ccb->intActive = true;
        //  Check if this interrupt is spurious
        if (!platform->intCtrl->beginInterrupt (ccb, context))
        {
            // This interrupt is spurious. Increase counter and return
            ++ccb->spuriousInts;
        }
        else
        {
            // Loop over the entire chain, trying to find a interrupt that can handle it
            NkSpinLock (&intObj->intChain->lock);
            NkList_t* chain = &intObj->intChain->list;
            NkLink_t* iter = NkListFront (chain);
            NkHwInterrupt_t* curInt = LINK_CONTAINER (iter, NkHwInterrupt_t, link);
            ipl_t oldIpl = ccb->curIpl;
            ccb->curIpl = curInt->ipl;    // Set IPL
            while (iter)
            {
                NkSpinUnlock (&intObj->intChain->lock);
                // Re-enable interrupts
                CpuEnable();
                if (curInt->handler (intObj, context))
                    break;    // Found one
                // Disable them again
                CpuDisable();
                NkSpinLock (&intObj->intChain->lock);
                iter = NkListIterate (chain, iter);
                curInt = LINK_CONTAINER (iter, NkHwInterrupt_t, link);
            }
            ccb->curIpl = oldIpl;    // Restore IPL
            // End the interrupt
            platform->intCtrl->endInterrupt (ccb, context);
        }
        ccb->intActive = false;
        // Make sure ints are enabled
        if (!preemptSet)
            TskEnablePreempt();    // Re-enable preemption
    }
    else
        assert (!"Invalid interrupt type");
}
