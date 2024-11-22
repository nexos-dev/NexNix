/*
    detect.c - contains generic DTB / ACPI detector
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

#include <nexke/nexboot.h>
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <nexke/platform/generic.h>

// Console struct externs
extern NkConsole_t fbCons;
extern NkConsole_t pl011Cons;

static NkPlatform_t nkPlatform = {0};

// Sets up platform drivers
void PltInitDrvs()
{
    // Initialize platform
    nkPlatform.type = PLT_TYPE_GENERIC;
    nkPlatform.subType = 0;
    // Initialize framebuffer console
    if (!NkGetBootArgs()->displayDefault)
    {
        NkFbConsInit();
        nkPlatform.primaryCons = &fbCons;
    }
    // Initialize lists
    NkListInit (&nkPlatform.cpus);
    NkListInit (&nkPlatform.intCtrls);
    NkListInit (&nkPlatform.ints);
    // Setup ACPI
    if (!PltAcpiInit())
    {
        // Just crash
        if (nkPlatform.primaryCons)
            nkPlatform.primaryCons->write ("nexke: fatal error: system doesn't support ACPI");
        CpuCrash();
    }
    // Get DBG2 table to find serial port
    void* dbgTab = PltAcpiFindTableEarly ("DBG2");
    if (dbgTab)
    {
        //  Grab table
        AcpiDbg2_t* dbg = dbgTab;
        // Iterate through each descriptor
        AcpiDbgDesc_t* desc = dbgTab + dbg->devDescOff;
        for (int i = 0; i < dbg->numDesc; ++i)
        {
            // Check type / subtype to see if it is supported
            if (desc->portType == ACPI_DBG_PORT_SERIAL)
            {
#ifdef NEXNIX_BASEARCH_ARM
                // Check sub-type
                if (desc->portSubtype == ACPI_DBG_PORT_PL011)
                {
                    // Initialize it
                    AcpiGas_t* gas = (void*) desc + desc->barOffset;
                    PltPL011Init (gas);
                    if (!nkPlatform.primaryCons)
                        nkPlatform.primaryCons = &pl011Cons;
                    nkPlatform.secondaryCons = &pl011Cons;
                }
#endif
            }
            desc = (void*) desc + desc->len;
        }
    }
}

// Adds CPU to platform
void PltAddCpu (PltCpu_t* cpu)
{
    NkListAddBack (&nkPlatform.cpus, &cpu->link);
    ++nkPlatform.numCpus;
    NkLogDebug ("nexke: found CPU, interrupt controller %s, ID %d\n",
                pltCpuTypes[cpu->type],
                cpu->id);
}

// Adds interrupt to platform
void PltAddInterrupt (PltIntOverride_t* intSrc)
{
    NkListAddFront (&nkPlatform.ints, &intSrc->link);
    NkLogDebug ("nexke: found interrupt override, line %d, bus %s, mode %s, polarity %s, GSI %u\n",
                intSrc->line,
                pltBusTypes[intSrc->bus],
                (intSrc->mode == PLT_MODE_EDGE) ? "edge" : "level",
                (intSrc->polarity == PLT_POL_ACTIVE_HIGH) ? "high" : "low",
                intSrc->gsi);
}

// Adds interrupt controller to platform
void PltAddIntCtrl (PltIntCtrl_t* intCtrl)
{
    NkListAddBack (&nkPlatform.intCtrls, &intCtrl->link);
    ++nkPlatform.numIntCtrls;
    NkLogDebug ("nexke: found interrupt controller, type %s, base GSI %u, address %#llX\n",
                pltIntCtrlTypes[intCtrl->type],
                intCtrl->gsiBase,
                intCtrl->addr);
}

// Resolves an interrupt line from bus-specific to a GSI
uint32_t PltGetGsi (int bus, int line)
{
    // If we are using 8259A, don't worry about doing this
    if (nkPlatform.intCtrl->type == PLT_INTCTRL_8259A)
        return line;
    // Search through interrupt overrides
    NkLink_t* iter = NkListFront (&nkPlatform.ints);
    while (iter)
    {
        PltIntOverride_t* intSrc = LINK_CONTAINER (iter, PltIntOverride_t, link);
        // Check if this is it
        if (intSrc->bus == bus && intSrc->line == line)
            return intSrc->gsi;
        iter = NkListIterate (iter);
    }
    return line;
}

// Gets an interrupt override based on the GSI
PltIntOverride_t* PltGetOverride (uint32_t gsi)
{
    // Search through interrupt overrides
    NkLink_t* iter = NkListFront (&nkPlatform.ints);
    while (iter)
    {
        PltIntOverride_t* intSrc = LINK_CONTAINER (iter, PltIntOverride_t, link);
        // Check if this is it
        if (intSrc->gsi == gsi)
            return intSrc;
        iter = NkListIterate (iter);
    }
    return NULL;
}

// Initializes system inerrupt controller
PltHwIntCtrl_t* PltInitHwInts()
{
#ifdef NEXNIX_BASEARCH_ARM
    PltHwIntCtrl_t* ctrl = PltGicInit();
    if (!ctrl)
        NkPanic ("nexke: can't find interrupt controller");
    nkPlatform.intCtrl = ctrl;
#endif
}

void PltInitPhase2()
{
    PltInitInterrupts();
}

void PltInitPhase3()
{
    if (nkPlatform.primaryCons == &fbCons)
        NkFbConsFbRemap();
    if (!PltAcpiDetectCpus())
        NkPanic ("nexke: CPU detection failed\n");
    // Initialize interrupt controller for architecture
    PltInitHwInts();
}

// Returns platform
NkPlatform_t* PltGetPlatform()
{
    return &nkPlatform;
}

// Gets primary console
NkConsole_t* PltGetPrimaryCons()
{
    return nkPlatform.primaryCons;
}

// Gets secondary console
NkConsole_t* PltGetSecondaryCons()
{
    return nkPlatform.secondaryCons;
}
