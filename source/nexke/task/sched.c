/*
    sched.c - contains thread scheduler for nexke
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
#include <nexke/task.h>
#include <string.h>

// Globals (try to avoid these)
static PltHwClock_t* clock = NULL;

// NOTE: most routines in here are interrupt-unsafe, but do not actually disable interrupts
// It's the caller's responsibilty to disable interrupts

// NOTE2: we use the list inlines differently here. So that we don't have to LINK_CONTAINER
// we keep the link at the start of the thread structure and simple cast back and forth
// This helps performance some

// NOTE3: the public interfaces all lock the run queue so the caller doesn't have to

// NOTE: most routines that use threads will lock the thread before use. Ordering is queue followed
// by thread

// Idle thread routine
static void TskIdleThread (void*)
{
    for (;;)
        CpuHalt();
}

// Admits thread to ready queue
// If this thread was preempted, it's added to the front;
// otherwise, its added to the tail
// IPL must be high and run queue must be locked
static FORCEINLINE void tskReadyThread (NkCcb_t* ccb, NkThread_t* thread)
{
    assert (PltGetIpl() == PLT_IPL_HIGH);
    // Check if we were preempted
    if (thread->preempted)
    {
        thread->preempted = false;    // Reset flag as preemption doesn't matter anymore
        // Only add to front if this wasn't due to quantum expirt
        if (thread->quantaLeft == 0)
            NkListAddBack (&ccb->readyQueue, &thread->link);
        else
            NkListAddFront (&ccb->readyQueue, &thread->link);
    }
    else
    {
        NkListAddBack (&ccb->readyQueue, &thread->link);    // For FCFS
    }
    // Reset quantum of thread
    thread->quantaLeft = thread->quantum;
    thread->state = TSK_THREAD_READY;
}

// Hook to prepare thread to stop running and let another thread run
static FORCEINLINE void tskStopThread (NkCcb_t* ccb, NkThread_t* thread)
{
    NkSpinLock (&thread->lock);
    assert (PltGetIpl() == PLT_IPL_HIGH);
    // Update runtime of thread
    thread->runTime += (clock->getTime() - thread->lastSchedule);
    // Figure out state
    if (thread->state == TSK_THREAD_RUNNING)
    {
        // If this is not the idle thread, add it to run queue
        if (!(thread->flags & TSK_THREAD_IDLE))
            tskReadyThread (ccb, thread);    // Admit it to run queue
    }
    else if (thread->state == TSK_THREAD_WAITING)
        TskThreadSetAssert (thread, 0);
    NkSpinUnlock (&thread->lock);
}

// Sets the current thread
// NOTE: call with interrupts disabled
static FORCEINLINE void tskSetCurrentThread (NkCcb_t* ccb, NkThread_t* thread)
{
    assert (PltGetIpl() == PLT_IPL_HIGH);
    // Set new thread as current
    thread->state = TSK_THREAD_RUNNING;
    // Stop the thread
    // Set last schedule time
    thread->lastSchedule = clock->getTime();
    // Get current thread and stop it
    NkThread_t* oldThread = ccb->curThread;
    assert (oldThread);
    tskStopThread (ccb, oldThread);
    // Make it current
    ccb->curThread = thread;
    // Do a context swap
    CpuSwitchContext (thread->context, &oldThread->context);
    // NOTE: from the CPU's perspective, we return from CpuSwitchContext in the new thread
    // From oldThread's perspective, it pauses, and then whenever it gets queue again, returns here
    // From thread's perspective, it resumes from a previous pause here
    // This is the only place where CpuSwitchContext gets called in the system
    // (besides the below function)
}

// Schedules a thread to execute
// The main interface to the scheduler
// NOTE: interrupt unsafe, call with IPL raised and run queue locked
static FORCEINLINE void tskSchedule (NkCcb_t* ccb)
{
    assert (PltGetIpl() == PLT_IPL_HIGH);
    // Grab next thread and current thread
    NkThread_t* nextThread = (NkThread_t*) NkListFront (&ccb->readyQueue);
    if (!nextThread)
    {
        // We either keep going or idle
        // This depends on the thread's state
        if (ccb->curThread->state == TSK_THREAD_RUNNING)
            return;                      // Don't do anything
        nextThread = ccb->idleThread;    // Run idle thread
    }
    else
        NkListRemove (&ccb->readyQueue, (NkLink_t*) nextThread);
    // Execute the thread
    tskSetCurrentThread (ccb, nextThread);
}

// Performs the initial task switch
// Basically the same as above, just assumes there isn't and old thread
// And is callable by the outside world
void __attribute__ ((noreturn)) TskSetInitialThread (NkThread_t* thread)
{
    NkCcb_t* ccb = CpuGetCcb();
    thread->state = TSK_THREAD_RUNNING;
    // Set last schedule time
    thread->lastSchedule = clock->getTime();
    // Set quanta left
    thread->quantaLeft = thread->quantum;
    // Set as current
    ccb->curThread = thread;
    CpuContext_t* fakeCtx = NULL;    // Fake context pointer
    CpuSwitchContext (thread->context, &fakeCtx);
    // UNREACHABLE
    assert (0);
}

// Preempts current thread
static FORCEINLINE void tskPreempt()
{
    assert (PltGetIpl() == PLT_IPL_HIGH);
    NkCcb_t* ccb = CpuGetCcb();
    NkThread_t* curThread = ccb->curThread;
    curThread->preempted = true;
    if (ccb->preemptDisable)
        ccb->preemptReq = true;    // Preemption has been requested
    else
    {
        ccb->preemptReq = false;
        TskLockRq (ccb);
        tskSchedule (ccb);    // Schedule the next thread
        TskUnlockRq (ccb);
    }
}

// Enables preemption
// IPL safe
void TskEnablePreemptUnsafe()
{
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    NkCcb_t* ccb = CpuGetCcb();
    assert (!ccb->preemptDisable);
    if (ccb->preemptReq)
        tskPreempt();    // Preempt the current thread
    PltLowerIpl (ipl);
}

// Gets current thread
NkThread_t* TskGetCurrentThread()
{
    return CpuGetCcb()->curThread;
}

// Inline function wrappers
void TskSetCurrentThread (NkThread_t* thread)
{
    NkCcb_t* ccb = CpuGetCcb();
    TskLockRq (ccb);
    tskSetCurrentThread (ccb, thread);
    TskUnlockRq (ccb);
}

void TskReadyThread (NkThread_t* thread)
{
    NkCcb_t* ccb = CpuGetCcb();
    // Lock 'er up
    TskLockRq (ccb);
    TskLockThread (thread);
    // Ready this thread
    tskReadyThread (ccb, thread);
    // Unlock it
    TskUnlockThread (thread);
    TskUnlockRq (ccb);
}

void TskSchedule()
{
    NkCcb_t* ccb = CpuGetCcb();
    TskLockRq (ccb);
    tskSchedule (ccb);
    TskUnlockRq (ccb);
}

// In this module for performance reasons
void TskWakeObj (TskWaitObj_t* obj)
{
    NkCcb_t* ccb = CpuGetCcb();
    NkThread_t* thread = obj->waiter;
    // Lock
    TskLockRq (ccb);
    TskLockThread (thread);
    tskReadyThread (ccb, thread);
    // Unlock
    TskUnlockThread (thread);
    TskUnlockRq (ccb);
}

// Time slice handler
static void TskTimeSlice (NkTimeEvent_t* evt, void* arg)
{
    ipl_t ipl = PltRaiseIpl (PLT_IPL_HIGH);
    // Get current thread
    NkCcb_t* ccb = CpuGetCcb();
    NkThread_t* curThread = ccb->curThread;
    NkSpinLock (&curThread->lock);
    // Check for quantum expiry
    if (curThread->quantaLeft == 0)
        tskPreempt();    // Mark it for preemption
    else
        --curThread->quantaLeft;
    NkSpinUnlock (&curThread->lock);
    PltLowerIpl (ipl);
}

// Initializes scheduler
void TskInitSched()
{
    // Create the idle thread
    NkCcb_t* ccb = CpuGetCcb();
    ccb->preemptDisable = 0;
    ccb->idleThread = TskCreateThread (TskIdleThread, NULL, "TskIdleThread", TSK_THREAD_IDLE);
    assert (ccb->idleThread);
    // Get clock
    clock = PltGetPlatform()->clock;
    // Set initial time slicing event
    NkTimeEvent_t* evt = NkTimeNewEvent();
    assert (evt);
    NkTimeSetCbEvent (evt, TskTimeSlice, NULL);
    NkTimeRegEvent (evt, TSK_TIMESLICE_DELTA, NK_TIME_REG_PERIODIC);
}
