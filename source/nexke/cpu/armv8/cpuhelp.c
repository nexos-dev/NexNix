/*
    cpuhelp.c - contains CPU helper functions for ARM
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

#include <nexke/cpu.h>
#include <nexke/nexke.h>
#include <stdio.h>

// Gets current exception level
int CpuGetEl()
{
    return CpuReadSpr ("CurrentEL");
}

// Disables interrupts
void CpuDisable()
{
    CpuWriteSpr ("daif", CPU_ARMV8_INT_D | CPU_ARMV8_INT_A | CPU_ARMV8_INT_I | CPU_ARMV8_INT_F);
}

// Enables interrupts
void CpuEnable()
{
    if (CpuGetCcb()->archCcb.intHold)
        CpuGetCcb()->archCcb.intReq = true;
    else
        CpuWriteSpr ("daif", 0);
}

void CpuHoldInts()
{
    CpuGetCcb()->archCcb.intHold = true;
}

void CpuUnholdInts()
{
    CpuGetCcb()->archCcb.intHold = false;
    if (CpuGetCcb()->archCcb.intReq)
    {
        CpuWriteSpr ("daif", 0);
        CpuGetCcb()->archCcb.intReq = false;
    }
}

void CpuHalt()
{
    asm ("wfi");
}

void CpuPrintDebug (CpuIntContext_t* context)
{
    // Basically we just dump all the registers
    va_list ap;
    NkLogMessage ("CPU dump:\n", NK_LOGLEVEL_EMERGENCY, ap);
    char buf[2048] = {0};
    sprintf (buf,
             "x0: %#016llX x1: %#016llX x2: %#016llX\nx3: %#016llX x4: %#016llX x5:%#016llX\nx6: "
             "%#016llX x7: %#016llX ",
             context->x0,
             context->x1,
             context->x2,
             context->x3,
             context->x4,
             context->x5,
             context->x6,
             context->x7);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    sprintf (
        buf,
        "x8: %#016llX\nx9: %#016llX x10: %#016llX x11: %#016llX\nx12: %#016llX x13: %#016llX x14: "
        "%#016llX\nx15: %#016llX ",
        context->x8,
        context->x9,
        context->x10,
        context->x11,
        context->x12,
        context->x13,
        context->x14,
        context->x15);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    sprintf (buf,
             "x16: %#016llX x17: %#016llX\nx18: %#016llX x19: %#016llX x20: %#016llX\nx21: "
             "%#016llX x22: %#016llX x23: %#016llX\n",
             context->x16,
             context->x17,
             context->x18,
             context->x19,
             context->x20,
             context->x21,
             context->x22,
             context->x23);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    sprintf (buf,
             "x24: %#016llX x25: %#016llX x26: %#016llX\nx27: %#016llX x28: %#016llX x29: "
             "%#016llX\nx30: %#016llX sp_el0: %#016llX ",
             context->x24,
             context->x25,
             context->x26,
             context->x27,
             context->x28,
             context->x29,
             context->x30,
             context->spEl0);
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
    sprintf (buf,
             "elr_el1: %#016llX\nspsr_el1: %#016llX tpidr_el1: %#016llX hndlr: %#02llX\nesr_el1: "
             "%#016llX far_el1: %#016llX\n",
             context->elr,
             context->spsr,
             context->tpidr,
             context->handler,
             CpuReadSpr ("ESR_EL1"),
             CpuReadSpr ("FAR_EL1"));
    NkLogMessage (buf, NK_LOGLEVEL_EMERGENCY, ap);
}
