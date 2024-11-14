/*
    mul.c - contains MMU management layer
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
#include <nexke/mm.h>
#include <nexke/nexke.h>
#include <string.h>

// Is INVLPG support?
static bool isInvlpg = false;

// Kernel directory version
static int mulKeVersion = 0;

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
    pte_t* pd = (pte_t*) CpuReadCr3();
    // On i386, we don't need to allocate a special page table for the page table cache
    // This is because the stack is mapped in that table already, so it has been created for us
    // But we do need to map the pages necessary for the cache to operate
    // So allocate cache entry page
    MmPage_t* cachePgCtrl = MmAllocFixedPage();
    paddr_t cachePage = cachePgCtrl->pfn * NEXKE_CPU_PAGESZ;
    // Map it
    MmMulMapEarly (MUL_PTCACHE_ENTRY_BASE, cachePage, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    // Map the page table for the table cache
    paddr_t cacheTab = pd[MUL_IDX_LEVEL (MUL_PTCACHE_TABLE_BASE, 2)] & PT_FRAME;
    MmMulMapEarly (MUL_PTCACHE_TABLE_BASE, cacheTab, MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    // Map kernel directory
    MmMulMapEarly ((uintptr_t) NEXKE_KERNEL_DIRBASE,
                   (paddr_t) pd,
                   MUL_PAGE_KE | MUL_PAGE_R | MUL_PAGE_RW);
    // Clear out all user PDEs
    memset (pd, 0, MUL_MAX_USER * sizeof (pte_t));
    // Write out CR3 to flush TLB
    CpuWriteCr3 ((uint32_t) pd);
    // Determine if invlpg is supported
    if (CpuGetFeatures() & CPU_FEATURE_INVLPG)
        isInvlpg = true;
    // Setup MUL
    MmMulSpace_t* mulSpace = &MmGetKernelSpace()->mulSpace;
    memset (mulSpace, 0, sizeof (MmMulSpace_t));
    mulSpace->base = (paddr_t) pd;
    mulSpace->keVersion = 0;
    mulSpace->refCount = 1;
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
    if (!(pte1 & PF_US) && (pte2 & PF_US))
        NkPanic ("nexke: error: can't map user mapping into kernel memory");
}

// Allocates page table into ent
paddr_t MmMulAllocTable (MmSpace_t* space, uintptr_t addr, pte_t* stBase, pte_t* ent)
{
    // Figure out if this is a kernel address
    bool isKernel = false;
    if (MmMulIsKernel (addr))
    {
        isKernel = true;
        assert (space == MmGetKernelSpace());
        ++mulKeVersion;
    }
    else
        isKernel = false;
    // Allocate the table
    MmPage_t* pg = MmAllocFixedPage();
    paddr_t tab = pg->pfn * NEXKE_CPU_PAGESZ;
    // Zero it
    MmMulZeroPage (pg);
    // Add to page list
    NkListAddFront (&space->mulSpace.pageList, &pg->link);
    // Set PTE
    pte_t flags = PF_P | PF_RW;
    if (!isKernel)
        flags |= PF_US;
    // Map it
    *ent = tab | flags;
    return tab;
}

void MmMulFlushCacheEntry (uintptr_t addr)
{
    if (isInvlpg)
        MmMulFlush (addr);
    else
        MmMulFlushTlb();
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

// Invalidates TLB
static inline bool MmMulFlushAddr (MmSpace_t* space, uintptr_t addr)
{
    bool flush = false;
    // Flush TLB if needed
    if (space == MmGetCurrentSpace() || space == MmGetKernelSpace())
    {
        if (isInvlpg)
            MmMulFlush (addr);
        else
        {
            // If this is a user mapping, simply mark this space for flushing when we return to user
            // mode If a kernel mapping, we unfortunatly have to do it know
            if (space != MmGetKernelSpace())
                space->mulSpace.tlbUpdatePending = true;
            else
                flush = true;
        }
    }
    return flush;
}

// Translates mapping flags
static pte_t mmMulGetProt (int perm)
{
    pte_t pgFlags = PF_P | PF_US;
    if (perm & MUL_PAGE_RW)
        pgFlags |= PF_RW;
    if (perm & MUL_PAGE_KE)
        pgFlags &= ~(PF_US);
    if (perm & MUL_PAGE_CD)
        pgFlags |= PF_CD;
    if (perm & MUL_PAGE_WT)
        pgFlags |= PF_WT;
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
    // Create PTE
    pte_t newPte = pgFlags | (page->pfn * NEXKE_CPU_PAGESZ);
    // Grab page table of last entry
    MmPtCacheEnt_t* cacheEnt = MmPtabWalkAndMap (space, mulSpace->base, virt, newPte);
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
            oldPage = MmFindPagePfn (*pte >> NEXKE_CPU_PAGE_SHIFT);
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
    if (MmMulFlushAddr (space, virt))
        MmMulFlushTlb();
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

// Unmaps a range out of an address space
void MmMulUnmapRange (MmSpace_t* space, uintptr_t base, size_t count)
{
    MM_MUL_LOCK (space);
    bool flushTlb = false;
    // Set up iterator
    MmPtIter_t iter = {0};
    iter.addr = base;
    iter.space = space;
    iter.asPhys = space->mulSpace.base;
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
                MmPage_t* page = MmFindPagePfn (*pte >> NEXKE_CPU_PAGE_SHIFT);
                *pte = 0;
                flushTlb = (flushTlb) ? true : MmMulFlushAddr (space, addr);
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
    if (flushTlb)
        MmMulFlushTlb();
    MM_MUL_UNLOCK (space);
}

// Changes protection on range of address space
void MmMulProtectRange (MmSpace_t* space, uintptr_t base, size_t count, int perm)
{
    MM_MUL_LOCK (space);
    // Get right flags
    pte_t flags = mmMulGetProt (perm);
    bool flushTlb = false;
    // Set up iterator
    MmPtIter_t iter = {0};
    iter.addr = base;
    iter.space = space;
    iter.asPhys = space->mulSpace.base;
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
                flushTlb = (flushTlb) ? true : MmMulFlushAddr (space, addr);
            }
        }
    }
    MmPtabEndIterate (&iter);
    if (flushTlb)
        MmMulFlushTlb();
    MM_MUL_UNLOCK (space);
}

// Unmaps a page and removes all its mappings
void MmMulUnmapPage (MmPage_t* page)
{
    // Loop through every mapping
    MmPageMap_t* map = page->maps;
    bool flushTlb = false;    // If we should flush the entire TLB
    while (map)
    {
        // Lock MUL
        MM_MUL_LOCK (map->space);
        MmMulSpace_t* mulSpace = &map->space->mulSpace;
        // Get cache entry
        MmPtCacheEnt_t* cacheEnt = MmPtabWalk (map->space, mulSpace->base, map->addr);
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
        flushTlb = (flushTlb) ? true : MmMulFlushAddr (map->space, map->addr);
    next:
        // Return and unlock
        MmPtabReturnCache (cacheEnt);
        MM_MUL_UNLOCK (map->space);
        map = map->next;
    }
    if (flushTlb)
        MmMulFlushTlb();
    page->maps = NULL;
}

// Changes protection on a page
void MmMulProtectPage (MmPage_t* page, int perm)
{
    // Get right flags
    pte_t flags = mmMulGetProt (perm);
    // Loop through every mapping
    MmPageMap_t* map = page->maps;
    bool flushTlb = false;    // If we should flush the entire TLB
    while (map)
    {
        // Lock MUL
        MM_MUL_LOCK (map->space);
        MmMulSpace_t* mulSpace = &map->space->mulSpace;
        // Get cache entry
        MmPtCacheEnt_t* cacheEnt = MmPtabWalk (map->space, mulSpace->base, map->addr);
        if (!cacheEnt)
            goto next;
        // Get PTE
        pte_t* table = (pte_t*) cacheEnt->addr;
        pte_t* pte = &table[MUL_IDX_LEVEL (map->addr, 1)];
        // Set flags
        *pte = (*pte & PT_FRAME) | flags | (*pte & PF_F);
        // Flush TLB if needed
        flushTlb = (flushTlb) ? true : MmMulFlushAddr (map->space, map->addr);
    next:
        // Return and unlock
        MmPtabReturnCache (cacheEnt);
        MM_MUL_UNLOCK (map->space);
        map = map->next;
    }
    if (flushTlb)
        MmMulFlushTlb();
    page->maps = NULL;
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
        // Get cache entry
        MmPtCacheEnt_t* cacheEnt = MmPtabWalk (map->space, mulSpace->base, map->addr);
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
        // Get cache entry
        MmPtCacheEnt_t* cacheEnt = MmPtabWalk (map->space, mulSpace->base, map->addr);
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
    MmPtCacheEnt_t* cacheEnt = MmPtabWalk (space, space->mulSpace.base, virt);
    assert (cacheEnt);
    // Get PTE
    pte_t* table = (pte_t*) cacheEnt->addr;
    pte_t* pte = &table[MUL_IDX_LEVEL (virt, 1)];
    paddr_t addr = *pte & PT_FRAME;
    MmPtabReturnCache (cacheEnt);
    MM_MUL_UNLOCK (space);
    return MmFindPagePfn (addr >> NEXKE_CPU_PAGE_SHIFT);
}

// MUL early routines

static pte_t* mulEarlyAllocTab (pde_t* pdir, uintptr_t virt, int flags)
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

// Maps a virtual address to a physical address early in the boot process
void MmMulMapEarly (uintptr_t virt, paddr_t phys, int flags)
{
    // Translate flags
    uint32_t pgFlags = PF_P | PF_US;
    if (flags & MUL_PAGE_RW)
        pgFlags |= PF_RW;
    if (flags & MUL_PAGE_KE)
        pgFlags &= ~(PF_US);
    if (flags & MUL_PAGE_CD)
        pgFlags |= PF_CD;
    if (flags & MUL_PAGE_WT)
        pgFlags |= PF_WT;
    // Get indices
    uint32_t dirIdx = PG_ADDR_DIR (virt);
    uint32_t tabIdx = PG_ADDR_TAB (virt);
    // Grab PDE
    pde_t* dir = (pde_t*) CpuReadCr3();
    pde_t* pde = &dir[dirIdx];
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
        pgTab = mulEarlyAllocTab (dir, virt, flags);
    }
    // Grab table entry
    pte_t* pte = &pgTab[tabIdx];
    if (*pte)
    {
        NkPanic ("nexke: cannot map mapped page");
    }
    *pte = phys | pgFlags;
    // Check for INVLPG
    if (CpuGetFeatures() & CPU_FEATURE_INVLPG)
        MmMulFlush (virt);
    else
        MmMulFlushTlb();
}

// Gets physical address of virtual address early in boot process
uintptr_t MmMulGetPhysEarly (uintptr_t virt)
{
    // Get indices
    uint32_t dirIdx = PG_ADDR_DIR (virt);
    uint32_t tabIdx = PG_ADDR_TAB (virt);
    // Grab PDE
    pde_t* dir = (pde_t*) CpuReadCr3();
    pde_t* pde = &dir[dirIdx];
    // Check if a table is mapped
    pte_t* pgTab = NULL;
    if (*pde)
    {
        // Get from PDE
        pgTab = (pte_t*) PT_GETFRAME (*pde);
    }
    else
    {
        NkPanic ("nexke: cannot get physical address of non-existant page");
    }
    // Grab table entry
    pte_t* pte = &pgTab[tabIdx];
    return PT_GETFRAME (*pte);
}
