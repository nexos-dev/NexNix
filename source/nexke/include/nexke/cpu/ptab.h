/*
    ptab.h - contains page table management stuff
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

#ifndef _PTAB_H
#define _PTAB_H

#include <nexke/cpu.h>
#include <nexke/types.h>
#include <stdbool.h>

// Page table cache entry
typedef struct _ptcache
{
    uintptr_t addr;           // Address of this entry
    paddr_t ptab;             // Physical address of page table being mapped
    pte_t* pte;               // PTE we should use to map this to a physical address
    int level;                // Level of this cache entry
    bool inUse;               // If this entry is in use
    struct _ptcache* next;    // Next entry in list
    struct _ptcache* prev;
} MmPtCacheEnt_t;

typedef struct _pttabiter
{
    int curPte;                  // Current PTE in this table
    MmPtCacheEnt_t* cacheEnt;    // Cache entry for this table
} MmPtIterTable_t;

// PTE iterator
typedef struct _pteiter
{
    uintptr_t addr;      // Current address in iterator
    MmSpace_t* space;    // Address space we are iterating in
    paddr_t asPhys;      // Address space physical base
    MmPtCacheEnt_t* asCache;
    MmPtIterTable_t ptIters[MM_PTAB_MAX_LEVEL + 1];
} MmPtIter_t;

#define MM_PTAB_UNCACHED 0

typedef struct _mmspace
{
    paddr_t base;                                         // Physical base of top level table
    MmPtCacheEnt_t* ptFreeList;                           // List of free PT cache entries
    MmPtCacheEnt_t* ptLists[MM_PTAB_MAX_LEVEL + 1];       // List of in use entries for each level
    MmPtCacheEnt_t* ptListsEnd[MM_PTAB_MAX_LEVEL + 1];    // List tails for each levels
    bool tlbUpdatePending;                                // Is a TLB update pending?
                              // Used to lazily update the TLB on CPUs where that is slow
    int freeCount;             // Free number of cache entries
    NkList_t pageList;         // Page table pages
    int refCount;              // Number of references to this address space
    spinlock_t lock;           // Lock on address space
                               // Should be more granular but whatever
    spinlock_t ptCacheLock;    // Lock on PT cache
#ifdef NEXNIX_ARCH_I386
    int keVersion;    // Kernel page table version
#endif
} MmMulSpace_t;

#define MM_MUL_LOCK(space)                                   \
    NkSpinLock (&MmGetCurrentSpace()->mulSpace.ptCacheLock); \
    NkSpinLock (&space->mulSpace.lock)
#define MM_MUL_UNLOCK(space)              \
    NkSpinUnlock (&space->mulSpace.lock); \
    NkSpinUnlock (&MmGetCurrentSpace()->mulSpace.ptCacheLock);

// Initializes page table manager
void MmPtabInit (int numLevels);

// Walks to a page table entry and maps specfied value into it
MmPtCacheEnt_t* MmPtabWalkAndMap (MmSpace_t* space, paddr_t as, uintptr_t vaddr, pte_t pte);

// Walks to a pte and returns a cache entry
MmPtCacheEnt_t* MmPtabWalk (MmSpace_t* space, paddr_t as, uintptr_t vaddr);

// Iterates over PTEs in address space
MmPtCacheEnt_t* MmPtabIterate (MmPtIter_t* iter);

// Frees iterator
void MmPtabEndIterate (MmPtIter_t* iter);

// Initializes PT cache in specified space
void MmPtabInitCache (MmSpace_t* space);

// Grabs cache entry for table
MmPtCacheEnt_t* MmPtabGetCache (paddr_t ptab, int level);

// Returns cache entry
void MmPtabReturnCache (MmPtCacheEnt_t* cacheEnt);

// Frees cache entry to free list
void MmPtabFreeToCache (MmPtCacheEnt_t* cacheEnt);

// Returns entry and gets new entry
MmPtCacheEnt_t* MmPtabSwapCache (paddr_t ptab, MmPtCacheEnt_t* cacheEnt, int level);

// Flushes a single TLB entry
void MmMulFlush (uintptr_t vaddr);

#endif
