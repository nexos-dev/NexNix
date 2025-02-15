/*
    ptab.c - contains architecture independent page table manager
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
#include <nexke/cpu/ptab.h>
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <string.h>

// Number of levels of paging structures
static int mmNumLevels = 0;

// Tunables
#define MM_PTAB_MINFREE    2
#define MM_PTAB_FREETARGET 8

// Initializes page table manager
void MmPtabInit (int numLevels)
{
    NkLogDebug ("nexke: MUL has %d levels\n", numLevels);
    mmNumLevels = numLevels;
}

// Walks to a page table entry and creates page tables for ita
MmPtCacheEnt_t* MmPtabWalkAndMap (MmSpace_t* space, paddr_t as, uintptr_t vaddr, pte_t pte)
{
    // Grab base and cache it
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (as, mmNumLevels);
    for (int i = mmNumLevels; i > 1; --i)
    {
        pte_t* curSt = (pte_t*) cacheEnt->addr;
        pte_t* ent = &curSt[MUL_IDX_LEVEL (vaddr, i)];
        if (*ent)
        {
            // Verify validity of this table for mapping this page
            // Failure results in panic
            MmMulVerify (*ent, pte);
            // Grab cache entry
            cacheEnt = MmPtabSwapCache (PT_GETFRAME (*ent), cacheEnt, i - 1);
        }
        else
        {
            // Allocate new table
            // This calls architecture specific code
            paddr_t newTab = MmMulAllocTable (space, vaddr, curSt, ent);
            cacheEnt = MmPtabSwapCache (newTab, cacheEnt, i - 1);
        }
    }
    // Returns last entry
    return cacheEnt;
}

// Walks to a pte and returns a cache entry
MmPtCacheEnt_t* MmPtabWalk (MmSpace_t* space, paddr_t as, uintptr_t vaddr)
{
    // Grab base and cache it
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (as, mmNumLevels);
    for (int i = mmNumLevels; i > 1; --i)
    {
        pte_t* curSt = (pte_t*) cacheEnt->addr;
        pte_t* ent = &curSt[MUL_IDX_LEVEL (vaddr, i)];
        if (*ent)
        {
            // Grab cache entry
            cacheEnt = MmPtabSwapCache (PT_GETFRAME (*ent), cacheEnt, i - 1);
        }
        else
        {
            // Table doesn't exist, panic
            NkPanic ("nexke: error: attempting to walk to invalid mapping");
        }
    }
    return cacheEnt;
}

// Iterates over PTEs in address space
MmPtCacheEnt_t* MmPtabIterate (MmPtIter_t* iter)
{
    // Cache address space
    if (!iter->asCache)
        iter->asCache = MmPtabGetCache (iter->asPhys, mmNumLevels);
    // Now get all other appropriate cache entries
    MmPtCacheEnt_t* cacheEnt = iter->asCache;
    uintptr_t addr = iter->addr;
    for (int i = mmNumLevels; i > 1; --i)
    {
        // Get next table
        MmPtIterTable_t* table = &iter->ptIters[i - 1];
        // Set current PTE in table
        table->curPte = MUL_IDX_LEVEL (addr, i - 1);
        // Check if it needs to be cached
        // This is true if we dont have a pre-existing cache entry or the current PTE goes to
        // zero
        if (!table->cacheEnt || table->curPte == 0)
        {
            // Return existing cache entry
            if (table->cacheEnt)
                MmPtabReturnCache (table->cacheEnt);
            // Get new table address
            pte_t* curSt = (pte_t*) cacheEnt->addr;
            pte_t* ent = &curSt[MUL_IDX_LEVEL (addr, i)];
            if (!*ent)
            {
                // Return NULL as PTE doesn't exist
                iter->addr += NEXKE_CPU_PAGESZ;
                return NULL;
            }
            // Cache it
            table->cacheEnt = MmPtabGetCache (PT_GETFRAME (*ent), i - 1);
        }
        cacheEnt = table->cacheEnt;
    }
    iter->addr += NEXKE_CPU_PAGESZ;    // To next page
    return cacheEnt;                   // Return the current cache entry
}

// Frees iterator
void MmPtabEndIterate (MmPtIter_t* iter)
{
    // Return every cached entry
    for (int i = 0; i < MM_PTAB_MAX_LEVEL; ++i)
    {
        MmPtIterTable_t* table = &iter->ptIters[i];
        if (table->cacheEnt)
            MmPtabReturnCache (table->cacheEnt);
    }
}

// Zeroes a page with the MUL
// This function is the same across MULs so it is implemented in the architecture independent
// module
void MmMulZeroPage (MmPage_t* page)
{
    NkSpinLock (&MmGetCurrentSpace()->mulSpace.ptCacheLock);
    // Get physical address
    paddr_t addr = page->pfn * NEXKE_CPU_PAGESZ;
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (addr, MM_PTAB_UNCACHED);
    memset ((void*) cacheEnt->addr, 0, NEXKE_CPU_PAGESZ);
    // Free the cache entry
    MmPtabFreeToCache (cacheEnt);
    NkSpinUnlock (&MmGetCurrentSpace()->mulSpace.ptCacheLock);
}

// Initializes PT cache in specified space
void MmPtabInitCache (MmSpace_t* space)
{
    // Initialize the cache entries
    MmPtCacheEnt_t* entries = (MmPtCacheEnt_t*) MUL_PTCACHE_ENTRY_BASE;
    for (int i = 0; i < MUL_MAX_PTCACHE; ++i)
    {
        entries[i].addr = MUL_PTCACHE_BASE + (i * NEXKE_CPU_PAGESZ);
        // Grab address of PTE for this address
        entries[i].pte = MmMulGetCacheAddr (entries[i].addr);
        // Check if this is the end
        if ((i + 1) == MUL_MAX_PTCACHE)
            entries[i].next = NULL;
        else
            entries[i].next = &entries[i + 1];
        if (i == 0)
            entries[i].prev = NULL;
        else
            entries[i].prev = &entries[i - 1];
    }
    NkLogDebug ("nexke: intialized page table cache at %p with %d entries\n",
                (uintptr_t) entries,
                MUL_MAX_PTCACHE);
    // Set pointer head
    space->mulSpace.ptFreeList = (MmPtCacheEnt_t*) MUL_PTCACHE_ENTRY_BASE;
    space->mulSpace.freeCount = MUL_MAX_PTCACHE;
}

// Removes entry from free list
static MmPtCacheEnt_t* mmPtabGetFree (MmSpace_t* space)
{
    assert (space->mulSpace.freeCount);
    MmMulSpace_t* mulSpace = &space->mulSpace;
    MmPtCacheEnt_t* ent = space->mulSpace.ptFreeList;
    mulSpace->ptFreeList = ent->next;
    if (mulSpace->ptFreeList)
        mulSpace->ptFreeList->prev = NULL;
    --mulSpace->freeCount;
    return ent;
}

// Adds entry to free list
static void mmPtabFreeEntry (MmSpace_t* space, MmPtCacheEnt_t* ent)
{
    MmMulSpace_t* mulSpace = &space->mulSpace;
    ent->next = mulSpace->ptFreeList;
    ent->prev = NULL;
    if (mulSpace->ptFreeList)
        mulSpace->ptFreeList->prev = ent;
    mulSpace->ptFreeList = ent;
    ++mulSpace->freeCount;
}

// Removes entry from a used list
static void mmPtabRemoveEntry (MmSpace_t* space, MmPtCacheEnt_t* ent)
{
    MmMulSpace_t* mulSpace = &space->mulSpace;
    int level = ent->level;
    MmPtCacheEnt_t** list = &mulSpace->ptLists[level];
    MmPtCacheEnt_t** tail = &mulSpace->ptListsEnd[level];
    if (ent->next)
        ent->next->prev = ent->prev;
    if (ent->prev)
        ent->prev->next = ent->next;
    if (ent == *list)
        (*list) = ent->next;
    if (ent == *tail)
        (*tail) = ent->prev;
}

// Adds entry to appropriate used list
static void mmPtabAddToList (MmSpace_t* space, MmPtCacheEnt_t* cacheEnt, int level)
{
    MmMulSpace_t* mulSpace = &space->mulSpace;
    MmPtCacheEnt_t** list = &mulSpace->ptLists[level];
    MmPtCacheEnt_t** tail = &mulSpace->ptListsEnd[level];
    cacheEnt->prev = NULL;
    cacheEnt->next = *list;
    if (*list)
        (*list)->prev = cacheEnt;
    else
    {
        *tail = cacheEnt;
        *list = cacheEnt;
    }
}

// Sets up cache entry
static void mmPtabSetupEntry (MmPtCacheEnt_t* ent, paddr_t ptab, int level)
{
    ent->inUse = true;
    ent->ptab = ptab;
    ent->level = level;
    MmMulMapCacheEntry (ent->pte, ent->ptab);
    MmMulFlushCacheEntry (ent->addr);
}

// Returns entry and gets new entry
MmPtCacheEnt_t* MmPtabSwapCache (paddr_t ptab, MmPtCacheEnt_t* cacheEnt, int level)
{
    MmPtabReturnCache (cacheEnt);
    return MmPtabGetCache (ptab, level);
}

// Grabs cache entry for table
MmPtCacheEnt_t* MmPtabGetCache (paddr_t ptab, int level)
{
    MmSpace_t* space = MmGetCurrentSpace();
    // Find entry on cache
    MmPtCacheEnt_t* ent = space->mulSpace.ptLists[level];
    while (ent)
    {
        if (ent->ptab == ptab)
        {
            // We found it
            ent->inUse = true;
            return ent;
        }
        ent = ent->next;
    }
    MmMulSpace_t* mulSpace = &space->mulSpace;
    // Attempt to find completly free entry
    if (mulSpace->freeCount)
    {
        // Take from free list
        ent = mmPtabGetFree (space);
        // Add to used list
        mmPtabAddToList (space, ent, level);
        // Set it up
        mmPtabSetupEntry (ent, ptab, level);
        return ent;
    }
    // No available cache entry, we need to evict one
    // Go through each entry by the tail and find an entry that is not in use so we can evict it
    for (int i = 0; i <= mmNumLevels; ++i)
    {
        MmPtCacheEnt_t* curEnt = mulSpace->ptListsEnd[i];
        while (curEnt)
        {
            if (!curEnt->inUse)
            {
                // Evict it
                mmPtabRemoveEntry (space, curEnt);
                mmPtabAddToList (space, curEnt, level);
                mmPtabSetupEntry (curEnt, ptab, level);
                return curEnt;
            }
        }
    }
    // TODO: We should block for an entry to be released
    // No blocking yet, so assert
    assert (false);
}

// Returns cache entry to cache
void MmPtabReturnCache (MmPtCacheEnt_t* cacheEnt)
{
    MmSpace_t* space = MmGetCurrentSpace();
    cacheEnt->inUse = false;
    // Check if we need to free any entries
    MmMulSpace_t* mulSpace = &space->mulSpace;
    if (mulSpace->freeCount < MM_PTAB_MINFREE)
    {
        // Go through every list and find entries to free
        // We go from the tail of each list as older entries are less likely to be used
        // according to the principle of LRU
        bool done = false;
        for (int i = 0; i <= mmNumLevels; ++i)
        {
            MmPtCacheEnt_t* ent = mulSpace->ptListsEnd[i];
            while (ent)
            {
                if (!ent->inUse)
                {
                    // Free this entry
                    mmPtabRemoveEntry (space, ent);
                    mmPtabFreeEntry (space, ent);
                    // Check if we've reached target
                    if (mulSpace->freeCount >= MM_PTAB_FREETARGET)
                    {
                        done = true;
                        break;
                    }
                }
                ent = ent->prev;
            }
            // If done, break
            if (done)
                break;
        }
    }
}

// Frees cache entry to free list
void MmPtabFreeToCache (MmPtCacheEnt_t* cacheEnt)
{
    MmSpace_t* space = MmGetCurrentSpace();
    cacheEnt->inUse = false;
    mmPtabRemoveEntry (space, cacheEnt);
    mmPtabFreeEntry (space, cacheEnt);
}
