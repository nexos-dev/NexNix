/*
    timer.c - contains ARM generic timer driver
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
#include <nexke/platform/generic.h>

extern PltHwClock_t gtClock;
extern PltHwTimer_t gtTimer;

// Gets time on clock
static ktime_t CpuGtGetTime()
{
}

// Polls for specified NS
static void CpuGtPoll (ktime_t delta)
{
}

// Arms timer to specified delta
static void CpuGtArmTimer (ktime_t delta)
{
}

PltHwClock_t gtClock = {.type = PLT_CLOCK_GENERIC, .getTime = CpuGtGetTime, .poll = CpuGtPoll};
PltHwTimer_t gtTimer = {.type = PLT_TIMER_GENERIC, .armTimer = CpuGtArmTimer};

// Initializes generic timer clock
PltHwClock_t* CpuInitGtClock()
{
    return &gtClock;
}

// Initializes generic timer timer
PltHwTimer_t* CpuInitGtTimer()
{
    return &gtTimer;
}
