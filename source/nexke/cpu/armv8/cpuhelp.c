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
