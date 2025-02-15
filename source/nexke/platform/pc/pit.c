/*
    pit.c - contains driver for PIT timer
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
#include <nexke/nexke.h>
#include <nexke/platform.h>
#include <nexke/platform/pc.h>

// PIT ports
#define PLT_PIT_CHAN0    0x40
#define PLT_PIT_CHAN1    0x41
#define PLT_PIT_CHAN2    0x42
#define PLT_PIT_MODE_CMD 0x43

// PIT frequency
#define PLT_PIT_FREQUENCY 1193180
#define PLT_PIT_HZ        100

// PIT mode / cmd register bits
#define PLT_PIT_BCD        (1 << 0)
#define PLT_PIT_ONESHOT    (0)
#define PLT_PIT_HW_ONESHOT (1 << 1)
#define PLT_PIT_RATEGEN    (2 << 1)
#define PLT_PIT_SQWAVE     (3 << 1)
#define PLT_PIT_SW_STROBE  (4 << 1)
#define PLT_PIT_HW_STROBE  (5 << 1)
#define PLT_PIT_LATCH      (0)
#define PLT_PIT_LOHI       (3 << 4)
#define PLT_PIT_SEL_CHAN0  (0)
#define PLT_PIT_SEL_CHAN1  (1 << 6)
#define PLT_PIT_SEL_CHAN2  (2 << 6)
#define PLT_PIT_READBACK   (3 << 6)

extern PltHwTimer_t pitTimer;
extern PltHwClock_t pitClock;

typedef struct _pitpvt
{
    bool isPitClock;      // Is PIT being used for clock
    int armCount;         // The number of remaining arms to complete interval
    uint16_t finalArm;    // THe final arm value
} pltPitPrivate_t;

static pltPitPrivate_t pitPvt = {0};
static NkHwInterrupt_t pitInt = {0};

// Arms timer to delta
static void PltPitArmTimer (ktime_t delta)
{
    if (pitPvt.armCount)
        pitPvt.armCount = 0, pitPvt.finalArm = 0;
    // Convert delta from nanosec precision to PIT precision
    delta /= pitTimer.precision;
    // If delta is now zero, round to 1 so we have a wait value
    if (!delta)
        ++delta;
    // Check if this is outside the native interval
    uint16_t maxInterval = pitTimer.maxInterval / pitTimer.precision;
    if (delta > maxInterval)
    {
        // Figure out the number of arms we will need to do
        pitPvt.armCount = delta / maxInterval;
        pitPvt.finalArm = delta % maxInterval;
        // Set delta to max
        delta = maxInterval;
    }
    // Set it
    CpuOutb (PLT_PIT_CHAN0, delta & 0xFF);
    CpuOutb (PLT_PIT_CHAN0, delta >> 8);
}

// PIT clock interrupt handler
static bool PltPitDispatch (NkInterrupt_t* intObj, CpuIntContext_t* ctx)
{
    // Check if PIT is in periodic mode or not
    if (pitPvt.isPitClock)
        pitClock.internalCount += pitClock.precision;    // Increase count
    // Check if we have any pending arms
    if (pitPvt.armCount)
    {
        --pitPvt.armCount;
        if (!pitPvt.armCount)
        {
            // Set it to final arm
            CpuOutb (PLT_PIT_CHAN0, pitPvt.finalArm & 0xFF);
            CpuOutb (PLT_PIT_CHAN0, pitPvt.finalArm >> 8);
        }
        else
        {
            CpuOutb (PLT_PIT_CHAN0, pitTimer.maxInterval & 0xFF);
            CpuOutb (PLT_PIT_CHAN0, pitTimer.maxInterval >> 8);
        }
    }
    else
    {
        // Call the callback. In periodic mode software must check for deadlines on every tick
        // In one shot this will drain the current deadline
        NkTimeHandler();
    }
    return true;
}

// Gets current PIT time
static ktime_t PltPitGetTime()
{
    return pitClock.internalCount;
}

// Polls for a amount of time
static void PltPitPoll (ktime_t time)
{
    ktime_t target = time + pitClock.internalCount;
    while (pitClock.internalCount < target)
        ;
}

PltHwTimer_t pitTimer = {.type = PLT_TIMER_PIT,
                         .armTimer = PltPitArmTimer,
                         .precision = 0,
                         .maxInterval = 0,
                         .private = (uintptr_t) &pitPvt};

PltHwClock_t pitClock = {.type = PLT_CLOCK_PIT,
                         .precision = 0,
                         .internalCount = 0,
                         .getTime = PltPitGetTime,
                         .poll = PltPitPoll};

// Installs PIT interrupt
static void PltPitInstallInt()
{
    // Prepare interrupt
    pitInt.gsi = PltGetGsi (PLT_BUS_ISA, PLT_PIC_IRQ_PIT);
    pitInt.mode = PLT_MODE_EDGE;
    pitInt.ipl = PLT_IPL_TIMER;
    pitInt.flags = 0;
    pitInt.handler = PltPitDispatch;
    if (!PltConnectInterrupt (&pitInt))
        NkPanic ("nexke: unable to install PIT interrupt");
}

// Initializes PIT clock
PltHwClock_t* PltPitInitClk()
{
    pltPitPrivate_t* pitPvt = (pltPitPrivate_t*) pitTimer.private;
    pitPvt->isPitClock = true;
    // Initialize PIT to perodic mode, with an interrupt every 10 ms
    CpuOutb (PLT_PIT_MODE_CMD, PLT_PIT_RATEGEN | PLT_PIT_LOHI | PLT_PIT_SEL_CHAN0);
    // Set divisor
    uint16_t div = PLT_PIT_FREQUENCY / PLT_PIT_HZ;
    CpuOutb (PLT_PIT_CHAN0, (uint8_t) div);
    CpuOutb (PLT_PIT_CHAN0, div >> 8);
    // Set precision of clock
    pitClock.precision = PLT_NS_IN_SEC / PLT_PIT_HZ;
    // Install interrupt handler
    PltPitInstallInt();
    NkLogDebug ("nexke: using PIT as clock, precision %uns\n", pitClock.precision);
    return &pitClock;
}

// Initializes PIT timer part
PltHwTimer_t* PltPitInitTimer()
{
    pltPitPrivate_t* pitPvt = (pltPitPrivate_t*) pitTimer.private;
    // Check if PIT is being used as clock
    if (pitPvt->isPitClock)
    {
        // In this case, we are actually a software timer. Basically we will call the callback
        // on every tick and the callback will manually trigger each event. This is slower
        // but is neccesary for old PCs with no invariant TSC, HPET, or ACPI PM timer
        NkLogDebug ("nexke: using software timer, precision %uns\n", pitClock.precision);
        pitTimer.type = PLT_TIMER_SOFT;
        pitTimer.precision = pitClock.precision;
    }
    else
    {
        // Otherwise, put it in one-shot mode and then we will arm the timer for each event
        // This is more precise then a software clock
        CpuOutb (PLT_PIT_MODE_CMD, PLT_PIT_ONESHOT | PLT_PIT_LOHI | PLT_PIT_SEL_CHAN0);
        pitTimer.precision = PLT_NS_IN_SEC / PLT_PIT_FREQUENCY;
        pitTimer.maxInterval = UINT16_MAX * (PLT_NS_IN_SEC / PLT_PIT_FREQUENCY);
        NkLogDebug ("nexke: using PIT as timer, precision %uns\n", pitTimer.precision);
        // Install the interrupt
        PltPitInstallInt();
    }
    return &pitTimer;
}
