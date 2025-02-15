/*
    tsc.c - contains TSC driver for x86
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
#include <nexke/platform.h>
#include <nexke/platform/pc.h>

extern PltHwClock_t tscClock;

int tscDivisor = 0;    // Value to divide/multply by for beyond nanosec precision

// Reads TSC
static inline ktime_t cpuTscRead()
{
    uint32_t low, high;
    asm volatile ("rdtsc" : "=a"(low), "=d"(high));
    return ((ktime_t) high << 32) | low;
}

// Converts from timestamp to ns
static inline ktime_t cpuFromTsc (ktime_t time)
{
    if (tscClock.precision == 1)
        return time / tscDivisor;
    return time * tscClock.precision;
}

// Converts ns to timestamp
static inline ktime_t cpuToTsc (ktime_t time)
{
    if (tscClock.precision == 1)
        return time * tscDivisor;
    return time / tscClock.precision;
}

// Gets time on clock
static ktime_t CpuTscGetTime()
{
    return cpuFromTsc (cpuTscRead());
}

// Poll for number of ns
static void CpuTscPoll (ktime_t time)
{
    ktime_t target = time + cpuFromTsc (cpuTscRead());
    while (target > cpuFromTsc (cpuTscRead()))
        ;
}

PltHwClock_t tscClock = {.type = PLT_CLOCK_TSC, .getTime = CpuTscGetTime, .poll = CpuTscPoll};

// Initialize TSC clock
PltHwClock_t* CpuInitTscClock()
{
    // Check for invariant TSC
    NkCcb_t* ccb = CpuGetCcb();
    if (!(ccb->archCcb.features & CPU_FEATURE_INVARIANT_TSC))
    {
        return NULL;    // TSC is unsuitable for our use
    }
    // In order to set up the TSC, we need another clock source to get precision
    // We'll use HPET, any system with an invariant TSC will basically always have an HPET
    PltHwClock_t* refClock = PltHpetInitClock();
    // Sample TSC once
    ktime_t start = cpuTscRead();
    refClock->poll (PLT_NS_IN_SEC / 100);    // 10 ms
    ktime_t end = cpuTscRead();
    ktime_t timeHz = (end - start) * 100;
    int precision = PLT_NS_IN_SEC / timeHz;
    if (!precision)
        ++precision;
    tscDivisor = timeHz / PLT_NS_IN_SEC;    // For beyond nanosec precision
    // Compensate for some rounding loss
    if ((timeHz % PLT_NS_IN_SEC) >= (PLT_NS_IN_SEC / 2))
        ++tscDivisor;    // Round up then
    if (!tscDivisor)
        ++tscDivisor;
    tscClock.precision = precision;
    NkLogDebug ("nexke: using TSC as clock, precision %ldns\n", tscClock.precision);
    return &tscClock;
}
