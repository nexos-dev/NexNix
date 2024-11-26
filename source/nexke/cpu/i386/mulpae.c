/*
    mulpae.c - contains MMU management layer for PAE systems
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
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <string.h>

// Kernel page directory template
static pte_t* mulKePgDir = NULL;

// Page mapping cache
static SlabCache_t* mulMapCache = NULL;

// Flushes whole TLB
void MmMulFlushTlb()
{
    CpuWriteCr3 (CpuReadCr3());
}

// Initializes MUL
void MmMulInit()
{
    NkLogDebug ("nexke: intializing MUL\n");
    MmPtabInit (2);    // Initialize page table manager with 2 levels
    // Grab page directory
    pte_t* pdpt = (pte_t*) CpuReadCr3();
    // On i386, we don't need to allocate a special page table for the page table cache
    // This is because the stack is mapped in that table already, so it has been created for us
    // But we do need to map the pages necessary for the cache to operate
    // So allocate cache entry page
    MmPage_t* cachePgCtrl = MmAllocFixedPage();
    MmFixPage (cachePgCtrl);
    paddr_t cachePage = cachePgCtrl->pfn * NEXKE_CPU_PAGESZ;
    // Map it
    MmMulMapEarly (MUL_PTCACHE_ENTRY_BASE, cachePage, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    // Map the page table for the table cache
    pte_t* dir = (pte_t*) (pdpt[PG_ADDR_PDPT (MUL_PTCACHE_ENTRY_BASE)] & PT_FRAME);
    paddr_t cacheTab = dir[PG_ADDR_DIR (MUL_PTCACHE_TABLE_BASE)] & PT_FRAME;
    MmMulMapEarly (MUL_PTCACHE_TABLE_BASE, cacheTab, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    // Clear out all user PDPTEs
    pdpt[0] = 0;
    pdpt[1] = 0;
    // Write out CR3 to flush TLB
    CpuWriteCr3 ((uint32_t) pdpt);
    // Setup MUL
    MmMulSpace_t* mulSpace = &MmGetKernelSpace()->mulSpace;
    memset (mulSpace, 0, sizeof (MmMulSpace_t));
    mulSpace->base = (paddr_t) pdpt;
    mulSpace->refCount = 1;
    NkListInit (&mulSpace->pageList);
    NkListAddFront (&mulSpace->pageList, &cachePgCtrl->link);
    // Prepare page table cache
    MmPtabInitCache (MmGetKernelSpace());
    mulMapCache = MmCacheCreate (sizeof (MmPageMap_t), "MmPageMap_t", 0, 0);
    assert (mulMapCache);
    // Set up PAT if we have it
    if (CpuGetFeatures() & CPU_FEATURE_PAT)
    {
        // Setup our PAT
        uint64_t pat = (MUL_PAT_WB << MUL_PAT0) | (MUL_PAT_WT << MUL_PAT1) |
                       (MUL_PAT_UCMINUS << MUL_PAT2) | (MUL_PAT_UC << MUL_PAT3) |
                       ((uint64_t) MUL_PAT_WC << MUL_PAT4);
        CpuWrmsr (MUL_PAT_MSR, pat);
    }
}

// Verifies mappability of pte2 into pte1
void MmMulVerify (pte_t pte1, pte_t pte2)
{
    // Make sure we aren't mapping user mapping into kernel region
    if (!(pte1 & PF_US) && pte2 & PF_US)
        NkPanic ("nexke: error: can't map user mapping into kernel memory");
}

// Allocates page table into ent
paddr_t MmMulAllocTable (MmSpace_t* space, uintptr_t addr, pte_t* stBase, pte_t* ent)
{
    // Figure out if this is a kernel address
    bool isKernel = false;
    if (MmMulIsKernel (addr))
        isKernel = true;
    else
        isKernel = false;
    MM_MUL_UNLOCK (space);
    // Allocate the table
    MmPage_t* pg = MmAllocFixedPage();
    MmFixPage (pg);
    paddr_t tab = pg->pfn * NEXKE_CPU_PAGESZ;
    // Zero it
    MmMulZeroPage (pg);
    MM_MUL_LOCK (space);
    // Check if a table was mapped while we were unlocked
    if (*ent)
        tab = *ent & PT_FRAME;
    else
    {
        // Add to page list
        NkListAddFront (&space->mulSpace.pageList, &pg->link);
        // Set PTE
        pte_t flags = PF_P | PF_RW;
        if (!isKernel)
            flags |= PF_US;
        // Map it
        *ent = tab | flags;
    }
    return tab;
}

// Allocates a page directory into ent
static paddr_t mulAllocDir (MmSpace_t* space, pdpte_t* ent)
{
    MM_MUL_UNLOCK (space);
    MmPage_t* pg = MmAllocFixedPage();
    MmFixPage (pg);
    paddr_t tab = pg->pfn * NEXKE_CPU_PAGESZ;
    // Zero it
    MmMulZeroPage (pg);
    MM_MUL_LOCK (space);
    if (*ent)
        tab = *ent & ~(PF_P);
    else
    {
        *ent = PF_P | tab;
        // Flush PDPTE registers on CPU
        if (space == MmGetCurrentSpace() || space == MmGetKernelSpace())
            MmMulFlushTlb();
        // Add to page list
        NkListAddFront (&space->mulSpace.pageList, &pg->link);
    }
    return tab;
}

// Creates an MUL address space
void MmMulCreateSpace (MmSpace_t* space)
{
}

// References an address space
void MmMulRefSpace (MmSpace_t* space)
{
    MM_MUL_LOCK (space);
    MmMulSpace_t* mulSpace = &space->mulSpace;
    ++mulSpace->refCount;
    MM_MUL_UNLOCK (space);
}

// Destroys an MUL address space
void MmMulDeRefSpace (MmSpace_t* space)
{
    if (space == MmGetKernelSpace())
        NkPanic ("nexke: can't destroy kernel space");
    MM_MUL_LOCK (space);
    MmMulSpace_t* mulSpace = &space->mulSpace;
    if (--mulSpace->refCount == 0)
    {
    }
    else
        MM_MUL_UNLOCK (space);
}

// Translates mapping flags
static inline pte_t mmMulGetProt (int perm)
{
    pte_t pgFlags = PF_P | PF_US;
    // Check for NX
    if (CpuGetFeatures() & CPU_FEATURE_XD)
        pgFlags |= PF_NX;
    if (perm & MUL_PAGE_RW)
        pgFlags |= PF_RW;
    if (perm & MUL_PAGE_KE)
        pgFlags &= ~(PF_US);
    if (perm & MUL_PAGE_CD)
        pgFlags |= PF_CD;
    if (perm & MUL_PAGE_DEV)
        pgFlags |= PF_CD;
    if (perm & MUL_PAGE_WT)
        pgFlags |= PF_WT;
    if (perm & MUL_PAGE_X)
        pgFlags &= ~(PF_NX);
    if (perm & MUL_PAGE_WC)
    {
        if (CpuGetFeatures() & CPU_FEATURE_PAT)
        {
            pgFlags |= PF_WC;
            // Clear PCD and PWT as they are mutually exclusive with our PAT setup
            pgFlags &= ~(PF_CD | PF_WT);
        }
        // Without PAT, WC equals WT
        else
            pgFlags |= PF_WT;
    }
    return pgFlags;
}

// Invalidates TLB
static inline void MmMulFlushAddr (MmSpace_t* space, uintptr_t addr)
{
    if (space == MmGetCurrentSpace() || space == MmGetKernelSpace())
        MmMulFlush (addr);
}

// Maps page into address space
void MmMulMapPage (MmSpace_t* space, uintptr_t virt, MmPage_t* page, int perm)
{
    MM_MUL_LOCK (space);
    MmMulSpace_t* mulSpace = &space->mulSpace;
    // Translate flags
    pte_t pgFlags = mmMulGetProt (perm);
    // Set fixed flag if needed
    if (page->flags & MM_PAGE_FIXED)
        pgFlags |= PF_F;
    // If this is a kernel page and global pages exist, make it global
    if (CpuGetFeatures() & CPU_FEATURE_PGE && MmMulIsKernel (virt))
        pgFlags |= PF_G;
    pte_t newPte = pgFlags | (page->pfn * NEXKE_CPU_PAGESZ);
    // Check if we need a new page directory
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (mulSpace->base, 3);
    pdpte_t* pdpt = (pdpte_t*) cacheEnt->addr;
    paddr_t pdir = 0;
    if (!(pdpt[PG_ADDR_PDPT (virt)] & PF_P))
    {
        // We need to allocate a page directory
        pdir = mulAllocDir (space, &pdpt[PG_ADDR_PDPT (virt)]);
    }
    else
    {
        pdir = pdpt[PG_ADDR_PDPT (virt)] & PT_FRAME;
    }
    MmPtabReturnCache (cacheEnt);
    cacheEnt = MmPtabWalkAndMap (space, pdir, virt, newPte);
    // Get table and PTE
    pte_t* table = (pte_t*) cacheEnt->addr;
    pte_t* pte = &table[MUL_IDX_LEVEL (virt, 1)];
    MmPage_t* oldPage = NULL;    // Set to the page we need to remove a mapping from
                                 // if the current PTE has a mapping in it
    // Check if the PTE already contains a translation
    if (*pte)
    {
        // Make sure current mapping isn't fixed
        if (*pte & PF_F)
            NkPanic ("nexke: attempt to unmap fixed mapping");
        // Check if the fixed state is changing
        if ((*pte & PF_F) != (newPte & PF_F))
        {
            // Keep stats accurate
            if (newPte & PF_F)
                ++space->stats.numFixed;
            else if (*pte & PF_F)
                --space->stats.numFixed;
        }
        // Check if we need to remove a mapping
        if ((*pte & PT_FRAME) != (newPte & PT_FRAME))
        {
            // We need to remove the old mapping
            // We can't do this until the address space is unlocked however
            // so just keep note of that
            oldPage = MmFindPagePfn ((*pte & PT_FRAME) >> NEXKE_CPU_PAGE_SHIFT);
            assert (oldPage);
        }
        // Set the PTE
        *pte = newPte;
    }
    else
    {
        // Check if this is fixed
        if (newPte & PF_F)
            ++space->stats.numFixed;
        // Set it
        *pte = newPte;
    }
    // Return it
    MmPtabReturnCache (cacheEnt);
    // Flush it
    MmMulFlushAddr (space, virt);
    MM_MUL_UNLOCK (space);
    // Check if we have a mapping to remove
    if (oldPage)
    {
        // Find this mapping
        NkSpinLock (&oldPage->lock);
        MmPageMap_t* map = oldPage->maps;
        MmPageMap_t* prev = NULL;
        while (map)
        {
            if (map->addr == virt && map->space == space)
            {
                // Remove it and break
                --space->stats.numMaps;
                if (prev)
                    prev->next = map->next;
                else
                    oldPage->maps = map->next;
                break;
            }
            prev = map;
            map = map->next;
        }
        NkSpinUnlock (&oldPage->lock);
    }
    if (!(page->flags & MM_PAGE_FIXED) && !(page->flags & MM_PAGE_UNUSABLE))
    {
        // Add this mapping
        MmPageMap_t* map = (MmPageMap_t*) MmCacheAlloc (mulMapCache);
        if (!map)
            NkPanicOom();
        map->addr = virt;
        map->space = space;
        map->next = NULL;
        // Link it
        NkSpinLock (&page->lock);
        map->next = page->maps;
        page->maps = map;
        NkSpinUnlock (&page->lock);
    }
    // Update stats
    ++space->stats.numMaps;
}

// Unmaps range of address space from a page directory
static inline void mmMulUnmapPdir (MmSpace_t* space, paddr_t dir, uintptr_t base, size_t count)
{
    // Set up iterator
    MmPtIter_t iter = {0};
    iter.addr = base;
    iter.space = space;
    iter.asPhys = dir;
    for (int i = 0; i < count; ++i)
    {
        uintptr_t addr = iter.addr;
        // Get cache entry for table containing PTE
        MmPtCacheEnt_t* cacheEnt = MmPtabIterate (&iter);
        // If there is no cache entry, move to next address
        if (cacheEnt)
        {
            // Get page table
            pte_t* table = (pte_t*) cacheEnt->addr;
            pte_t* pte = &table[MUL_IDX_LEVEL (addr, 1)];
            if (*pte)
            {
                // Make sure PTE isn't fixed
                if (*pte & PF_F)
                    NkPanic ("nexke: can't remove fixed mapping");
                // Get page of PTE
                MmPage_t* page = MmFindPagePfn ((*pte & PT_FRAME) >> NEXKE_CPU_PAGE_SHIFT);
                *pte = 0;
                MmMulFlushAddr (space, addr);
                // Now remove mapping, first we need to unlock the address space
                // as we can't lock the page while holding the address space lock
                // as that would violate lock ordering
                // I don't think this will cause any problems, but I could be wrong
                MM_MUL_UNLOCK (space);
                NkSpinLock (&page->lock);
                // Find the mapping
                MmPageMap_t* map = page->maps;
                MmPageMap_t* prev = NULL;
                while (map)
                {
                    if (map->addr == addr && map->space == space)
                    {
                        // Remove it and break
                        --space->stats.numMaps;
                        if (prev)
                            prev->next = map->next;
                        else
                            page->maps = map->next;
                        break;
                    }
                    prev = map;
                    map = map->next;
                }
                NkSpinUnlock (&page->lock);
                MM_MUL_LOCK (space);
            }
        }
    }
    MmPtabEndIterate (&iter);
}

// Protects a range in page directory
static inline void mmMulProtectPdir (MmSpace_t* space,
                                     paddr_t dir,
                                     uintptr_t base,
                                     size_t count,
                                     int perm)
{
    // Get right flags
    pte_t flags = mmMulGetProt (perm);
    // Set up iterator
    MmPtIter_t iter = {0};
    iter.addr = base;
    iter.space = space;
    iter.asPhys = dir;
    for (int i = 0; i < count; ++i)
    {
        uintptr_t addr = iter.addr;
        // Get cache entry for table containing PTE
        MmPtCacheEnt_t* cacheEnt = MmPtabIterate (&iter);
        // If there is no cache entry, move to next address
        if (cacheEnt)
        {
            // Get page table
            pte_t* table = (pte_t*) cacheEnt->addr;
            pte_t* pte = &table[MUL_IDX_LEVEL (addr, 1)];
            // Set protection if PTE is valid
            if (*pte & PF_P)
            {
                *pte = (*pte & PT_FRAME) | flags | (*pte & PF_F);
                MmMulFlushAddr (space, addr);
            }
        }
    }
    MmPtabEndIterate (&iter);
}

// Unmaps a range out of an address space
void MmMulUnmapRange (MmSpace_t* space, uintptr_t base, size_t count)
{
    MM_MUL_LOCK (space);
    // Get PDPT
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space->mulSpace.base, 3);
    pdpte_t* pdpt = (pdpte_t*) cacheEnt->addr;
    uintptr_t cur = base;
    uintptr_t end = base + (count * NEXKE_CPU_PAGESZ) - 1;
    while (end > cur)
    {
        // Check if it is valid
        if (!(pdpt[PG_ADDR_PDPT (cur)] & PF_P))
            NkPanic ("nexke: cannot unmap invalid address");
        paddr_t pdirAddr = pdpt[PG_ADDR_PDPT (cur)] & PT_FRAME;
        // Figure out how many page of this lie in this page directory
        // First align address up to 1GB boundary
#define MM_1GB_BOUND 0x40000000
        uintptr_t maxTop = NkAlignUp (cur, MM_1GB_BOUND) - 1;
        uintptr_t top = 0;
        // Pick min of maxTop and base + count
        if (end < maxTop)
            top = end;
        else
            top = maxTop;
        // Unmap this range
        mmMulUnmapPdir (space, pdirAddr, cur, CpuPageAlignUp (top - cur) / NEXKE_CPU_PAGESZ);
        // To next part
        cur = top;
    }
    MmPtabReturnCache (cacheEnt);
    MM_MUL_UNLOCK (space);
}

// Changes protection on range of address space
void MmMulProtectRange (MmSpace_t* space, uintptr_t base, size_t count, int perm)
{
    MM_MUL_LOCK (space);
    // Get PDPT
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space->mulSpace.base, 3);
    pdpte_t* pdpt = (pdpte_t*) cacheEnt->addr;
    uintptr_t cur = base;
    uintptr_t end = base + (count * NEXKE_CPU_PAGESZ) - 1;
    while (end > cur)
    {
        // Check if it is valid
        if (!(pdpt[PG_ADDR_PDPT (cur)] & PF_P))
            NkPanic ("nexke: cannot unmap invalid address");
        paddr_t pdirAddr = pdpt[PG_ADDR_PDPT (cur)] & PT_FRAME;
        // Figure out how many page of this lie in this page directory
        // First align address up to 1GB boundary
#define MM_1GB_BOUND 0x40000000
        uintptr_t maxTop = NkAlignUp (cur, MM_1GB_BOUND) - 1;
        uintptr_t top = 0;
        // Pick min of maxTop and base + count
        if (end < maxTop)
            top = end;
        else
            top = maxTop;
        // Unmap this range
        mmMulProtectPdir (space,
                          pdirAddr,
                          cur,
                          CpuPageAlignUp (top - cur) / NEXKE_CPU_PAGESZ,
                          perm);
        // To next part
        cur = top;
    }
    MmPtabReturnCache (cacheEnt);
    MM_MUL_UNLOCK (space);
}

// Unmaps a page and removes all its mappings
void MmMulUnmapPage (MmPage_t* page)
{
    // Loop through every mapping
    MmPageMap_t* map = page->maps;
    while (map)
    {
        // Lock MUL
        MM_MUL_LOCK (map->space);
        MmMulSpace_t* mulSpace = &map->space->mulSpace;
        // Get page directory
        MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (mulSpace->base, 3);
        pdpte_t* pdpt = (pdpte_t*) cacheEnt->addr;
        paddr_t pdir = 0;
        pdir = pdpt[PG_ADDR_PDPT (map->addr)] & PT_FRAME;
        if (!pdir)
            goto next;
        MmPtabReturnCache (cacheEnt);
        // Get page table
        cacheEnt = MmPtabWalk (map->space, pdir, map->addr);
        if (!cacheEnt)
            goto next;
        // Get PTE
        pte_t* table = (pte_t*) cacheEnt->addr;
        pte_t* pte = &table[MUL_IDX_LEVEL (map->addr, 1)];
        // Make sure it isn't fixed
        if (*pte & PF_F)
            NkPanic ("nexke: can't unmap fixed mapping");
        // Clear it
        *pte = 0;
        --map->space->stats.numMaps;
        // Flush TLB if needed
        MmMulFlushAddr (map->space, map->addr);
    next:
        // Return and unlock
        MmPtabReturnCache (cacheEnt);
        MM_MUL_UNLOCK (map->space);
        map = map->next;
    }
    page->maps = NULL;
}

// Changes protection on a page
void MmMulProtectPage (MmPage_t* page, int perm)
{
    // Get right flags
    pte_t flags = mmMulGetProt (perm);
    // Loop through every mapping
    MmPageMap_t* map = page->maps;
    while (map)
    {
        // Lock MUL
        MM_MUL_LOCK (map->space);
        MmMulSpace_t* mulSpace = &map->space->mulSpace;
        // Get page directory
        MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (mulSpace->base, 3);
        pdpte_t* pdpt = (pdpte_t*) cacheEnt->addr;
        paddr_t pdir = 0;
        pdir = pdpt[PG_ADDR_PDPT (map->addr)] & PT_FRAME;
        if (!pdir)
            goto next;
        MmPtabReturnCache (cacheEnt);
        // Get page table
        cacheEnt = MmPtabWalk (map->space, pdir, map->addr);
        if (!cacheEnt)
            goto next;
        // Get PTE
        pte_t* table = (pte_t*) cacheEnt->addr;
        pte_t* pte = &table[MUL_IDX_LEVEL (map->addr, 1)];
        // Set flags
        *pte = (*pte & PT_FRAME) | flags | (*pte & PF_F);
        // Flush TLB if needed
        MmMulFlushAddr (map->space, map->addr);
    next:
        // Return and unlock
        MmPtabReturnCache (cacheEnt);
        MM_MUL_UNLOCK (map->space);
        map = map->next;
    }
}

// Fixes a page in an address space
void MmMulFixPage (MmPage_t* page)
{
    assert (page->fixCount);
    // Loop through every mapping
    MmPageMap_t* map = page->maps;
    while (map)
    {
        // Lock MUL
        MM_MUL_LOCK (map->space);
        MmMulSpace_t* mulSpace = &map->space->mulSpace;
        // Get page directory
        MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (mulSpace->base, 3);
        pdpte_t* pdpt = (pdpte_t*) cacheEnt->addr;
        paddr_t pdir = 0;
        pdir = pdpt[PG_ADDR_PDPT (map->addr)] & PT_FRAME;
        if (!pdir)
            goto next;
        MmPtabReturnCache (cacheEnt);
        // Get page table
        cacheEnt = MmPtabWalk (map->space, pdir, map->addr);
        if (!cacheEnt)
            goto next;
        // Get PTE
        pte_t* table = (pte_t*) cacheEnt->addr;
        pte_t* pte = &table[MUL_IDX_LEVEL (map->addr, 1)];
        if (*pte == 0)
            goto next;
        // Check if this is actually changing the attribute
        if ((*pte & PF_F) == 0)
            ++map->space->stats.numFixed;
        // Set the attribute
        *pte |= PF_F;
    next:
        // Return and unlock
        MmPtabReturnCache (cacheEnt);
        MM_MUL_UNLOCK (map->space);
        map = map->next;
    }
}

// Unfixes a page
void MmMulUnfixPage (MmPage_t* page)
{
    // Loop through every mapping
    MmPageMap_t* map = page->maps;
    while (map)
    {
        // Lock MUL
        MM_MUL_LOCK (map->space);
        MmMulSpace_t* mulSpace = &map->space->mulSpace;
        // Get page directory
        MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (mulSpace->base, 3);
        pdpte_t* pdpt = (pdpte_t*) cacheEnt->addr;
        paddr_t pdir = 0;
        pdir = pdpt[PG_ADDR_PDPT (map->addr)] & PT_FRAME;
        if (!pdir)
            goto next;
        MmPtabReturnCache (cacheEnt);
        // Get page table
        cacheEnt = MmPtabWalk (map->space, pdir, map->addr);
        if (!cacheEnt)
            goto next;
        // Get PTE
        pte_t* table = (pte_t*) cacheEnt->addr;
        pte_t* pte = &table[MUL_IDX_LEVEL (map->addr, 1)];
        if (*pte == 0)
            goto next;
        // Check if this is actually changing the attribute
        if (*pte & PF_F)
            --map->space->stats.numFixed;
        // Set the attribute
        *pte &= ~(PF_F);
    next:
        // Return and unlock
        MmPtabReturnCache (cacheEnt);
        MM_MUL_UNLOCK (map->space);
        map = map->next;
    }
}

// Gets mapping for specified virtual address
MmPage_t* MmMulGetMapping (MmSpace_t* space, uintptr_t virt)
{
    MM_MUL_LOCK (space);
    // Get page directory to unmap
    MmPtCacheEnt_t* cacheEnt = MmPtabGetCache (space->mulSpace.base, 3);
    pdpte_t* pdpt = (pdpte_t*) cacheEnt->addr;
    // Check if it is valid
    if (!(pdpt[PG_ADDR_PDPT (virt)] & PF_P))
        NkPanic ("nexke: cannot unmap invalid address");
    paddr_t pdirAddr = pdpt[PG_ADDR_PDPT (virt)] & PT_FRAME;
    MmPtabReturnCache (cacheEnt);
    // Grab PTE
    cacheEnt = MmPtabWalk (space, pdirAddr, virt);
    assert (cacheEnt);
    // Get PTE
    pte_t* table = (pte_t*) cacheEnt->addr;
    pte_t* pte = &table[MUL_IDX_LEVEL (virt, 1)];
    paddr_t addr = *pte & PT_FRAME;
    MmPtabReturnCache (cacheEnt);
    MM_MUL_UNLOCK (space);
    return MmFindPagePfn (addr / NEXKE_CPU_PAGESZ);
}

// MUL early

static pte_t* mulAllocTabEarly (pde_t* pdir, uintptr_t virt, int flags)
{
    pte_t* tab = (pte_t*) MmMulGetPhysEarly ((uintptr_t) MmAllocKvPage());
    memset (tab, 0, NEXKE_CPU_PAGESZ);
    // Grab PDE
    pde_t* tabPde = &pdir[PG_ADDR_DIR (virt)];
    if (flags & MUL_PAGE_KE)
        *tabPde = (paddr_t) tab | PF_P | PF_RW;
    else
        *tabPde = (paddr_t) tab | PF_P | PF_RW | PF_US;
    // Return it
    return tab;
}

static pde_t* mulAllocDirEarly (pdpte_t* pdpt, uintptr_t virt)
{
    pde_t* dir = (pte_t*) MmMulGetPhysEarly ((uintptr_t) MmAllocKvPage());
    memset (dir, 0, NEXKE_CPU_PAGESZ);
    // Map it
    pdpt[PG_ADDR_PDPT (virt)] = PF_P | (paddr_t) dir;
    // Reload PDPTE registers
    uint32_t dirp = CpuReadCr3();
    CpuWriteCr3 (dirp);
    return dir;
}

// Maps a virtual address to a physical address early in the boot process
void MmMulMapEarly (uintptr_t virt, paddr_t phys, int flags)
{
    // Translate flags
    uint64_t pgFlags = PF_P | PF_US;
    if (flags & MUL_PAGE_RW)
        pgFlags |= PF_RW;
    if (flags & MUL_PAGE_KE)
        pgFlags &= ~(PF_US);
    if (flags & MUL_PAGE_CD)
        pgFlags |= PF_CD;
    if (flags & MUL_PAGE_WT)
        pgFlags |= PF_WT;
    if (flags & MUL_PAGE_DEV)
        pgFlags |= PF_CD;
    // Get indices
    uint32_t pdptIdx = PG_ADDR_PDPT (virt);
    uint32_t dirIdx = PG_ADDR_DIR (virt);
    uint32_t tabIdx = PG_ADDR_TAB (virt);
    // Grab PDPTE
    pdpte_t* pdpt = (pdpte_t*) CpuReadCr3();
    pdpte_t* pdpte = &pdpt[pdptIdx];
    pde_t* pdir = NULL;
    if (*pdpte)
    {
        // Get it
        pdir = (pde_t*) PT_GETFRAME (*pdpte);
    }
    else
    {
        // Allocate new directory
        pdir = mulAllocDirEarly (pdpt, virt);
    }
    pde_t* pde = &pdir[dirIdx];
    // Check if a table is mapped
    pte_t* pgTab = NULL;
    if (*pde)
    {
        // Get from PDE
        pgTab = (pte_t*) PT_GETFRAME (*pde);
    }
    else
    {
        // Allocate new page table
        pgTab = mulAllocTabEarly (pdir, virt, flags);
    }
    pte_t* pte = &pgTab[tabIdx];
    if (*pte)
        NkPanic ("nexke: error: cannot map mapped address");
    *pte = pgFlags | phys;
    MmMulFlush (virt);
}

// Gets physical address of virtual address early in boot process
uintptr_t MmMulGetPhysEarly (uintptr_t virt)
{
    // Get indices
    uint32_t pdptIdx = PG_ADDR_PDPT (virt);
    uint32_t dirIdx = PG_ADDR_DIR (virt);
    uint32_t tabIdx = PG_ADDR_TAB (virt);
    // Grab PDPTE
    pdpte_t* pdpt = (pdpte_t*) CpuReadCr3();
    pdpte_t* pdpte = &pdpt[pdptIdx];
    pde_t* dir = NULL;
    // Check if dir is mapped
    if (*pdpte)
        dir = (pde_t*) PT_GETFRAME (*pdpte);
    else
        NkPanic ("nexke: cannot get physical address of non-existant page");
    // Grab PDE
    pde_t* pde = &dir[dirIdx];
    // Check if a table is mapped
    pte_t* pgTab = NULL;
    if (*pde)
    {
        // Get from PDE
        pgTab = (pte_t*) PT_GETFRAME (*pde);
    }
    else
        NkPanic ("nexke: cannot get physical address of non-existant page");
    // Grab table entry
    pte_t* pte = &pgTab[tabIdx];
    return PT_GETFRAME (*pte);
}
