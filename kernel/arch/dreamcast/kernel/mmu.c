/* KallistiOS ##version##

   arch/dreamcast/kernel/mmu.c
   (c)2001 Megan Potter
   Copyright (C) 2026 Joseph Black
*/

/* SH-4 MMU related functions, ported up from KOS-MMU */

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

#include <arch/arch.h>
#include <arch/mmu.h>

#include <dc/memory.h>

#include <kos/cache.h>
#include <kos/dbgio.h>
#include <kos/irq.h>
#include <kos/thread.h>

#define MMU_IND_BITS 12                         /**< \brief Index bits */
#define MMU_VIRTUAL_PAGES (MMU_PAGES * MMU_SUB_PAGES)
#define MMU_PHYSICAL_PAGES (0x20000000u >> MMU_IND_BITS)
#define MMU_STATIC_MAX 62u

/********************************************************************************/
/* Register definitions */

static volatile uint32_t * const pteh = (uint32_t *)(SH4_REG_MMU_PTEH);
static volatile uint32_t * const ptel = (uint32_t *)(SH4_REG_MMU_PTEL);
//static volatile uint32_t * const ptea = (uint32_t *)(SH4_REG_MMU_PTEA);
static volatile uint32_t * const ttb = (uint32_t *)(SH4_REG_MMU_TTB);
static volatile uint32_t * const tea = (uint32_t *)(SH4_REG_MMU_TEA);
static volatile uint32_t * const mmucr = (uint32_t *)(SH4_REG_MMU_CR);

#define BUILD_PTEH(VA, ASID) \
    ( ((VA) & 0xfffffc00) | ((ASID) & 0xff) )

#define SET_PTEH(VA, ASID) \
    do { *pteh = BUILD_PTEH(VA, ASID); } while(0)

#define BUILD_PTEL(PA, V, SZ, PR, C, D, SH, WT) \
    ( ((PA) & 0x1ffffc00) | ((V) << 8) \
      | ( ((SZ) & 2) << 6 ) | ( ((SZ) & 1) << 4 ) \
      | ( (PR) << 5 ) \
      | ( (C) << 3 ) \
      | ( (D) << 2 ) \
      | ( (SH) << 1 ) \
      | ( (WT) << 0 ) )

#define SET_TTB(TTB) \
    do { *ttb = TTB; } while(0)

#define SET_MMUCR(URB, URC, SQMD, SV, TI, AT) \
    do { *mmucr = ((URB) << 18) \
                      | ((URC) << 10) \
                      | ((SQMD) << 9) \
                      | ((SV) << 8) \
                      | ((TI) << 2) \
                      | ((AT) << 0); } while(0)

#define SET_URC(URC) \
    do { *mmucr = (*mmucr & ~(63 << 10)) \
                      | (((URC) & 63) << 10); } while(0)

#define GET_URC() ((*mmucr >> 10) & 63)

#define INCR_URC() \
    do { SET_URC(GET_URC() + 1); } while(0)

/********************************************************************************/

/* "Current" page tables (for TLB exception handling) */
mmucontext_t *mmu_cxt_current = NULL;

/* This value will be non-zero if we can safely shortcut the standard tlb-miss
   exception handling. */
int mmu_shortcut_ok = 0;

/* The last URC value we used */
static int last_urc;

/* Our TLB mapping function */
static mmu_mapfunc_t map_func;

/* Number of static allocations */
static unsigned int tlb_nb_static;

/********************************************************************************/
/* Physical hardware management */

static inline void mmu_ldtlb_quick(uint32_t ptehv, uint32_t ptelv) {
    *pteh = ptehv;
    *ptel = ptelv;
    __asm__("ldtlb");
}

static inline void mmu_ldtlb(int asid, uint32_t virt, uint32_t phys, int sz, int pr, int c, int d,
                             int sh, int wt) {
    mmu_ldtlb_quick(BUILD_PTEH(virt, asid), BUILD_PTEL(phys, 1, sz, pr, c, d, sh, wt));
}

static inline void mmu_ldtlb_wait(void) {
    __asm__("nop");
    __asm__("nop");
    __asm__("nop");
    __asm__("nop");
    __asm__("nop");
    __asm__("nop");
    __asm__("nop");
    __asm__("nop");
}

/* Defined in mmuitlb.s */
void mmu_reset_itlb(void);
void mmu_invalidate_tlb(uint32_t virt, uint32_t asid);
void mmu_set_sq_addr_asm(uint32_t ptel1, uint32_t ptel2);

/* Defined below */
static mmupage_t *map_virt(mmucontext_t *context, int virtpage);

/********************************************************************************/
/* Table management */

/* Set the "current" page tables for TLB handling */
void mmu_use_table(mmucontext_t *context) {
    mmu_cxt_current = context;

    if(mmu_cxt_current && map_func == map_virt)
        mmu_shortcut_ok = 1;
    else
        mmu_shortcut_ok = 0;
}

/* Allocate a page table shell; no actual sub-contexts will be allocated
   until a mapping is performed. */
mmucontext_t *mmu_context_create(int asid) {
    mmucontext_t    *cont;
    int     i;

    if(asid < 0 || asid > 255) {
        errno = EINVAL;
        return NULL;
    }

    cont = (mmucontext_t*)malloc(sizeof(mmucontext_t));

    if(cont == NULL)
        return NULL;

    cont->asid = asid;

    for(i = 0; i < MMU_PAGES; i++)
        cont->sub[i] = NULL;

    return cont;
}

/* Destroy an MMU context when a process is being destroyed. */
void mmu_context_destroy(mmucontext_t *context) {
    int i;

    if(!context)
        return;

    {
        irq_disable_scoped();

        /* Even an inactive context can retain ASID-tagged TLB translations and
           dirty physical cache lines from its last use. Retire both before
           freeing it without pulling in the purge-all eviction workspace. */
        for(i = 0; i < MMU_PAGES; i++) {
            int j;

            if(!context->sub[i])
                continue;

            for(j = 0; j < MMU_SUB_PAGES; j++) {
                if(context->sub[i]->page[j].valid) {
                    mmupage_t *page = &context->sub[i]->page[j];
                    uint32_t virtpage = i * MMU_SUB_PAGES + j;

                    if(page->cache) {
                        uintptr_t physical =
                            (uintptr_t)page->physical << MMU_IND_BITS;

                        dcache_purge_range(physical | 0x80000000u, PAGESIZE);
                    }

                    mmu_invalidate_tlb(virtpage << MMU_IND_BITS,
                                       context->asid);
                }
            }
        }

        if(context == mmu_cxt_current)
            mmu_use_table(NULL);
    }

    for(i = 0; i < MMU_PAGES; i++) {
        if(context->sub[i] != NULL)
            free(context->sub[i]);
    }

    free(context);
}

/* Using the given page tables, return a pointer to the page entry
   matching the given virtual page ID, or return NULL if there
   isn't one. */
static mmupage_t *map_virt(mmucontext_t *context, int virtpage) {
    mmusubcontext_t *sub;
    mmupage_t   *page;
    int     top, bot;

    if(!context || virtpage < 0 || virtpage >= MMU_VIRTUAL_PAGES)
        return NULL;

    top = (unsigned int)virtpage / MMU_SUB_PAGES;
    bot = (unsigned int)virtpage % MMU_SUB_PAGES;

    /* Look up the top-level sub-context */
    sub = context->sub[top];

    if(sub == NULL)
        return NULL;

    /* Look up the bottom-level page */
    page = sub->page + bot;

    if(!page->valid)
        return NULL;

    /* Return the physical page number */
    return page;
}

/* Using the given page tables, translate the virtual page ID to a
   physical page ID. Return -1 on failure. */
int mmu_virt_to_phys(mmucontext_t *context, int virtpage) {
    mmupage_t   *page;

    page = map_virt(context, virtpage);

    if(!page)
        return -1;
    else
        return page->physical;
}

/* Return the first virtual page mapped to the requested physical page. */
int mmu_phys_to_virt(mmucontext_t *context, int physpage) {
    int top;

    if(!context || physpage < 0 ||
       (unsigned int)physpage >= MMU_PHYSICAL_PAGES)
        return -1;

    for(top = 0; top < MMU_PAGES; top++) {
        int bot;

        if(!context->sub[top])
            continue;

        for(bot = 0; bot < MMU_SUB_PAGES; bot++) {
            const mmupage_t *page = &context->sub[top]->page[bot];

            if(page->valid && page->physical == (unsigned int)physpage)
                return top * MMU_SUB_PAGES + bot;
        }
    }

    return -1;
}

/* Switch to the given context; invalidate any caches as necessary */
void mmu_switch_context(mmucontext_t *context) {
    SET_PTEH(0, context->asid);
}

static int validate_page_range(mmucontext_t *context, int virtpage,
                               int physpage, int count,
                               page_prot_t prot, page_cache_t cache) {
    if(!context || virtpage < 0 || physpage < 0 || count <= 0 ||
       prot < MMU_KERNEL_RDONLY || prot > MMU_ALL_RDWR ||
       cache < MMU_NO_CACHE || cache > MMU_CACHE_WT ||
       (unsigned int)virtpage >= MMU_VIRTUAL_PAGES ||
       (unsigned int)count > MMU_VIRTUAL_PAGES - (unsigned int)virtpage ||
       (unsigned int)physpage >= MMU_PHYSICAL_PAGES ||
       (unsigned int)count > MMU_PHYSICAL_PAGES - (unsigned int)physpage) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

static void page_set_cache(mmupage_t *page, page_cache_t cache) {
    page->cache = cache != MMU_NO_CACHE;
    page->wthru = cache == MMU_CACHE_WT;
}

static void page_compile(mmupage_t *page, int virtpage) {
    uint32_t virt = (uint32_t)virtpage << MMU_IND_BITS;

    page->pteh = BUILD_PTEH(virt, 0);
    page->ptel = BUILD_PTEL(page->physical << PAGESIZE_BITS, 1,
                            PAGE_SIZE_4K, page->prkey, page->cache,
                            page->dirty, page->shared, page->wthru);
}

static void purge_cached_range(mmucontext_t *context,
                               int virtpage, int count) {
    int i;

    for(i = 0; i < count; i++) {
        mmupage_t *page = map_virt(context, virtpage + i);

        if(page && page->cache) {
            uintptr_t physical =
                (uintptr_t)page->physical << MMU_IND_BITS;

            /* P1 spans the complete 29-bit physical address space and lets
               the cache operation retire lines from an inactive context. */
            dcache_purge_range(physical | 0x80000000u, PAGESIZE);
        }
    }
}

int mmu_page_map_ex(mmucontext_t *context,
                    int virtpage, int physpage, int count,
                    page_prot_t prot, page_cache_t cache,
                    bool share, bool dirty) {
    mmusubcontext_t **new_sub = NULL;
    int first_top, last_top, top_count, missing = 0, top, i;

    if(validate_page_range(context, virtpage, physpage, count,
                           prot, cache) < 0)
        return -1;

    first_top = virtpage / MMU_SUB_PAGES;
    last_top = (virtpage + count - 1) / MMU_SUB_PAGES;
    top_count = last_top - first_top + 1;

    for(top = first_top; top <= last_top; top++) {
        if(!context->sub[top])
            missing++;
    }

    if(missing) {
        new_sub = calloc(top_count, sizeof(*new_sub));
        if(!new_sub) {
            errno = ENOMEM;
            return -1;
        }
    }

    /* Allocate every missing second-level table before changing the mapping.
       The checked API is therefore all-or-nothing on allocation failure. */
    for(top = first_top; top <= last_top; top++) {
        if(!context->sub[top]) {
            int index = top - first_top;

            new_sub[index] = calloc(1, sizeof(*new_sub[index]));
            if(!new_sub[index]) {
                int rollback;

                for(rollback = 0; rollback < top_count; rollback++)
                    free(new_sub[rollback]);

                free(new_sub);
                errno = ENOMEM;
                return -1;
            }
        }
    }

    {
        irq_disable_scoped();

        /* An inactive context can retain dirty physical cache lines from its
           last use, so cache retirement cannot depend on current selection. */
        purge_cached_range(context, virtpage, count);

        for(top = first_top; top <= last_top; top++) {
            int index = top - first_top;

            if(new_sub && new_sub[index])
                context->sub[top] = new_sub[index];
        }

        for(i = 0; i < count; i++) {
            int page_id = virtpage + i;
            int page_top = page_id / MMU_SUB_PAGES;
            int page_bot = page_id % MMU_SUB_PAGES;
            mmupage_t *page = &context->sub[page_top]->page[page_bot];

            /* A previous context using the same ASID may have left a
               translation for this VPN even when this slot is empty. */
            mmu_invalidate_tlb((uint32_t)page_id << MMU_IND_BITS,
                               context->asid);

            page->physical = physpage + i;
            page->prkey = prot;
            page_set_cache(page, cache);
            page->dirty = dirty;
            page->blank = 0;
            page->shared = share;
            page->valid = 1;
            page_compile(page, page_id);
        }
    }

    free(new_sub);
    return 0;
}

/* Preserve the original source and binary interface while giving new code an
   error-reporting operation through mmu_page_map_ex(). */
void mmu_page_map(mmucontext_t *context,
                  int virtpage, int physpage, int count,
                  page_prot_t prot, page_cache_t cache,
                  bool share, bool dirty) {
    (void)mmu_page_map_ex(context, virtpage, physpage, count,
                          prot, cache, share, dirty);
}

static bool subcontext_empty(const mmusubcontext_t *sub) {
    int i;

    for(i = 0; i < MMU_SUB_PAGES; i++) {
        if(sub->page[i].valid)
            return false;
    }

    return true;
}

int mmu_page_unmap(mmucontext_t *context, int virtpage, int count) {
    int i;

    if(!context || virtpage < 0 || count <= 0 ||
       (unsigned int)virtpage >= MMU_VIRTUAL_PAGES ||
       (unsigned int)count > MMU_VIRTUAL_PAGES - (unsigned int)virtpage) {
        errno = EINVAL;
        return -1;
    }

    {
        irq_disable_scoped();

        purge_cached_range(context, virtpage, count);

        for(i = 0; i < count; i++) {
            int page_id = virtpage + i;
            int top = page_id / MMU_SUB_PAGES;
            int bot = page_id % MMU_SUB_PAGES;
            mmusubcontext_t *sub = context->sub[top];
            mmupage_t *page;

            if(!sub)
                continue;

            page = &sub->page[bot];
            if(page->valid) {
                mmu_invalidate_tlb((uint32_t)page_id << MMU_IND_BITS,
                                   context->asid);
                memset(page, 0, sizeof(*page));
            }
        }
    }

    /* Empty tables no longer represent any mapping and can be reclaimed. */
    for(i = virtpage / MMU_SUB_PAGES;
        i <= (virtpage + count - 1) / MMU_SUB_PAGES; i++) {
        if(context->sub[i] && subcontext_empty(context->sub[i])) {
            free(context->sub[i]);
            context->sub[i] = NULL;
        }
    }

    return 0;
}

int mmu_page_set_cache(mmucontext_t *context, int virtpage, int count,
                       page_cache_t cache) {
    int i;

    if(!context || virtpage < 0 || count <= 0 ||
       cache < MMU_NO_CACHE || cache > MMU_CACHE_WT ||
       (unsigned int)virtpage >= MMU_VIRTUAL_PAGES ||
       (unsigned int)count > MMU_VIRTUAL_PAGES - (unsigned int)virtpage) {
        errno = EINVAL;
        return -1;
    }

    for(i = 0; i < count; i++) {
        if(!map_virt(context, virtpage + i)) {
            errno = ENOENT;
            return -1;
        }
    }

    irq_disable_scoped();

    purge_cached_range(context, virtpage, count);

    for(i = 0; i < count; i++) {
        int page_id = virtpage + i;
        mmupage_t *page = map_virt(context, page_id);

        mmu_invalidate_tlb((uint32_t)page_id << MMU_IND_BITS,
                           context->asid);
        page_set_cache(page, cache);
        page_compile(page, page_id);
    }

    return 0;
}

#if 0   /* Only applies to KOS-MMU */
/* Syscall version of mmu_page_map; all parameters are adjusted to
   even page boundaries; if src is NULL, anonymous pages are mapped
   (allocated from the heap pool); if src is non-NULL, the address
   is considered to be a physical address. Use munmap to free them. */
void sc_mmu_mmap(uint32_t dst, size_t len, uint32_t src) {
    int anon = 0;

    /* Adjust length to page boundary */
    if(len & PAGEMASK)
        len = (len & ~PAGEMASK) + PAGESIZE;

    len >>= PAGESIZE_BITS;

    /* If no src pointer, then allocate anonymous pages */
    if(!src) {
        src = (uint32_t)mm_palloc(len, proc_current->pid);

        if(src == 0)
            RETURN(0);

        anon = 1;
    }

    /* Do the actual mapping */
    /*dbgio_printf("sc: mmu_page_map(%08x,%08x,%08x,%08x,%d,%d,%d,%d)\n",
        proc_current->pt, dst >> PAGESIZE_BITS, src >> PAGESIZE_BITS, len,
        MMU_ALL_RDWR, anon ? MMU_CACHEABLE : MMU_NO_CACHE, MMU_SHARED, MMU_DIRTY); */
    mmu_page_map(proc_current->pt,
                 dst >> PAGESIZE_BITS, src >> PAGESIZE_BITS, len,
                 MMU_ALL_RDWR,
                 anon ? MMU_CACHEABLE : MMU_NO_CACHE,
                 MMU_NOT_SHARED,
                 MMU_DIRTY);
    RETURN(dst);
}
#endif /* 0 */

/* Copy a chunk of data from a process' address space into a
   kernel buffer, taking into account page mappings.

   This routine is pretty nasty.. this is completely platform
   generic but should probably be replaced by a nice assembly
   routine for each platform as appropriate. */
int mmu_copyin(mmucontext_t *context, uint32_t srcaddr, uint32_t srccnt, void *buffer) {
    mmupage_t *srcpage;
    uint32_t srcptr;
    uint32_t src, run;
    int copied, srckrn;
    uint8_t *dst;

    /* Setup source pointers */
    srcptr = (uint32_t)srcaddr;

    if(!(srcptr & 0x80000000)) {
        srcpage = map_virt(context, srcptr >> PAGESIZE_BITS);

        if(srcpage == NULL)
            arch_panic("mmu_copyv with invalid source page");

        src = (srcpage->physical << PAGESIZE_BITS) | (srcptr & PAGEMASK);
        srckrn = 0;
    }
    else {
        src = srcptr;
        srckrn = 1;
    }

    /* Setup destination pointers */
    dst = (uint8_t*)buffer;

    /* Do the actual copy */
    copied = 0;

    while(srccnt > 0) {
        /* Determine the largest run we can get away with */

        /* What's left of source page */
        run = PAGESIZE - (srcptr & PAGEMASK);

        /* What's left of source count */
        if(srccnt < run)
            run = srccnt;

        /* Do the segment copy */
        memcpy(dst, (void*)(src | 0x80000000), run);

        /* Adjust all the pointers */
        src += run;
        srcptr += run;
        dst += run;

        /* Check for overruns */
        srccnt -= run;

        if(!srckrn && (srcptr & ~PAGEMASK) != ((srcptr - run) & ~PAGEMASK)) {
            srcpage = map_virt(context, srcptr >> PAGESIZE_BITS);

            if(srcpage == NULL)
                arch_panic("mmu_copyv with invalid source page (in loop)");

            src = (srcpage->physical << PAGESIZE_BITS)
                  | (srcptr - (srcptr & ~PAGEMASK));
        }

        copied += run;
    }

    return copied;
}

/* Copy a chunk of data from one process' address space to another
   process' address space, taking into account page mappings.

   This routine is pretty nasty.. this is completely platform
   generic but should probably be replaced by a nice assembly
   routine for each platform as appropriate. */
int mmu_copyv(mmucontext_t *context1, struct iovec *iov1, int iovcnt1,
              mmucontext_t *context2, struct iovec *iov2, int iovcnt2) {
    mmupage_t *srcpage, *dstpage;
    int srciov, dstiov;
    uint32_t srccnt, dstcnt;
    uint32_t srcptr, dstptr;
    uint32_t src, dst, run;
    int copied;
    int srckrn, dstkrn;
    /* static int   sproket = 0; */

    /* timer_disable_primary();
    irq_enable(); */

    /* Setup source pointers */
    srciov = 0;
    srccnt = iov1[srciov].iov_len;
    srcptr = (uint32_t)iov1[srciov].iov_base;

    if(!(srcptr & 0x80000000)) {
        srcpage = map_virt(context1, srcptr >> PAGESIZE_BITS);

        if(srcpage == NULL)
            arch_panic("mmu_copyv with invalid source page");

        src = (srcpage->physical << PAGESIZE_BITS) | (srcptr & PAGEMASK);
        srckrn = 0;
    }
    else {
        src = srcptr;
        srckrn = 1;
    }

    /* Setup destination pointers */
    dstiov = 0;
    dstcnt = iov2[dstiov].iov_len;
    dstptr = (uint32_t)iov2[dstiov].iov_base;

    if(!(dstptr & 0x80000000)) {
        dstpage = map_virt(context2, dstptr >> PAGESIZE_BITS);

        if(dstpage == NULL)
            arch_panic("mmu_copyv with invalid destination page");

        dst = (dstpage->physical << PAGESIZE_BITS) | (dstptr & PAGEMASK);
        dstkrn = 0;
    }
    else {
        dst = dstptr;
        dstkrn = 1;
    }

    /* Do the actual copy */
    copied = 0;

    while(srciov < iovcnt1 && dstiov < iovcnt2) {
        /* Determine the largest run we can get away with */

        /* What's left of source page */
        run = PAGESIZE - (srcptr & PAGEMASK);

        /* What's left of destination page */
        if((PAGESIZE - (dstptr & PAGEMASK)) < run)
            run = PAGESIZE - (dstptr & PAGEMASK);

        /* What's left of source iov */
        if(srccnt < run)
            run = srccnt;

        /* What's left of dest iov */
        if(dstcnt < run)
            run = dstcnt;

        /* Do the segment copy */
        /* if(!sproket) {
            dbgio_printf("Copying %08lx -> %08lx (%08lx -> %08lx), %d bytes\n",
                srcptr, dstptr, src, dst, run);
            dbgio_flush();
            dbgio_flush();
            dbgio_flush();
            dbgio_flush();
            sproket = 1;
        } */
        //debug();
        memcpy((void*)(dst | 0xa0000000), (void*)(src | 0x80000000), run);
        /* dcache_inval_range(dstptr, run); */
        dcache_inval_range(dst | 0x80000000, run);
        //undebug();

        /* Adjust all the pointers */
        src += run;
        srcptr += run;
        dst += run;
        dstptr += run;
        copied += run;

        /* Check for overruns */
        srccnt -= run;

        if(srccnt <= 0) {
            srciov++;

            if(srciov >= iovcnt1) break;

            srccnt = iov1[srciov].iov_len;
            srcptr = (uint32_t)iov1[srciov].iov_base;

            if(!srckrn) {
                srcpage = map_virt(context1, srcptr >> PAGESIZE_BITS);

                if(srcpage == NULL)
                    arch_panic("mmu_copyv with invalid source page (in loop)");

                src = (srcpage->physical << PAGESIZE_BITS) | (srcptr & PAGEMASK);
            }
            else {
                src = srcptr;
            }
        }
        else {
            if(!srckrn && (srcptr & ~PAGEMASK) != ((srcptr - run) & ~PAGEMASK)) {
                srcpage = map_virt(context1, srcptr >> PAGESIZE_BITS);

                if(srcpage == NULL)
                    arch_panic("mmu_copyv with invalid source page (in loop)");

                src = (srcpage->physical << PAGESIZE_BITS)
                      | (srcptr - (srcptr & ~PAGEMASK));
            }
        }

        dstcnt -= run;

        if(dstcnt <= 0) {
            dstiov++;

            if(dstiov >= iovcnt2) break;

            dstcnt = iov2[dstiov].iov_len;
            dstptr = (uint32_t)iov2[dstiov].iov_base;

            if(!dstkrn) {
                dstpage = map_virt(context2, dstptr >> PAGESIZE_BITS);

                if(dstpage == NULL)
                    arch_panic("mmu_copyv with invalid destination page (in loop)");

                dst = (dstpage->physical << PAGESIZE_BITS) | (dstptr & PAGEMASK);
            }
            else {
                dst = dstptr;
            }
        }
        else {
            if(!dstkrn && (dstptr & ~PAGEMASK) != ((dstptr - run) & ~PAGEMASK)) {
                dstpage = map_virt(context2, dstptr >> PAGESIZE_BITS);

                if(dstpage == NULL)
                    arch_panic("mmu_copyv with invalid destination page (in loop)");

                dst = (dstpage->physical << PAGESIZE_BITS)
                      | (dstptr - (dstptr & ~PAGEMASK));
            }
        }
    }

    return copied;
}


/********************************************************************************/
/* Exception handlers */

mmu_mapfunc_t mmu_map_get_callback(void) {
    return map_func;
}

mmu_mapfunc_t mmu_map_set_callback(mmu_mapfunc_t newfunc) {
    mmu_mapfunc_t tmp = map_func;
    map_func = newfunc;

    if(mmu_cxt_current && map_func == map_virt)
        mmu_shortcut_ok = 1;
    else
        mmu_shortcut_ok = 0;

    return tmp;
}

static void unhandled_mmu(irq_t source, irq_context_t *context) {
    int i;

    (void)source;

    dbgio_printf("Exception happened in tid %d at PC %08lx, SR %08lx\n",
                 thd_current->tid, context->pc, context->sr);
    dbgio_printf(" PTEH = %08lx, PTEL = %08lx\n", *pteh, *ptel);
    dbgio_printf(" TTB = %08lx, TEA = %08lx\n", *ttb, *tea);
    dbgio_printf(" MMUCR = %08lx\n", *mmucr);
    dbgio_printf(" PR = %08lx\n", context->pr);

    for(i = 0; i < 512; i++)
        dbgio_flush();

    arch_panic("unhandled MMU exception");
}

/* Generic handler that takes a missed TLB exception and loads the
   appropriate entry into the UTLB. */
void mmu_gen_tlb_miss(const char *what, irq_t source, irq_context_t *context) {
    mmupage_t *page;
    uint32_t addr, ptehv, ptelv;

    /* Get the offending reference */
    addr = *tea;

    /* Do we have a mapping func? */
    if(!map_func) {
        dbgio_printf("%s: no mapping function to map address %08lx!\n",
                     what, addr);
        unhandled_mmu(source, context);
    }

    /* Do we have page tables? */
    if(map_func == map_virt && !mmu_cxt_current) {
        dbgio_printf("%s: no page tables installed to map address %08lx!\n",
                     what, addr);
        unhandled_mmu(source, context);
    }

    /* Translate it to the proper physical address */
    page = map_func(mmu_cxt_current, addr >> PAGESIZE_BITS);

    if(!page) {
        dbgio_printf("%s: cannot map virtual address %08lx\n", what, addr);
        unhandled_mmu(source, context);
    }

    /* Make sure we don't overwrite the last TLB entry */
    /* if(GET_URC() == last_urc) {
        last_urc++;
        SET_URC(last_urc);
    } else {
        last_urc = GET_URC();
    } */

    /* Load the mapping */
    //dbgio_printf("asid %d: loading up mapping %08x -> %08x, prkey=%d into %x\n",
    //  proc_current->pt->asid, *tea, page->physical << PAGESIZE_BITS, page->prkey, last_urc);
    ptehv = page->pteh | mmu_cxt_current->asid;
    ptelv = page->ptel;
    mmu_ldtlb_quick(ptehv, ptelv);
    /* mmu_ldtlb(mmu_cxt_current->asid, *tea, page->physical << PAGESIZE_BITS, 1, page->prkey,
        page->cache, page->dirty, page->shared, page->wthru);
    mmu_ldtlb_wait(); */
}

/* Instruction TLB miss exception */
static void itlb_miss(irq_t source, irq_context_t *context, void *data) {
    (void)data;
    mmu_gen_tlb_miss("itlb_miss", source, context);
}

/* Instruction TLB protection violation */
static void itlb_pv(irq_t source, irq_context_t *context, void *data) {
    (void)data;
    dbgio_printf("itlb_pv\n");
    unhandled_mmu(source, context);
}

/* Should eventually handle data address read/write here */

/* Data TLB miss (read) */
static void dtlb_miss_read(irq_t source, irq_context_t *context, void *data) {
    (void)data;
    mmu_gen_tlb_miss("dtlb_miss_read", source, context);
}

/* Data TLB miss (write) */
static void dtlb_miss_write(irq_t source, irq_context_t *context, void *data) {
    (void)data;
    mmu_gen_tlb_miss("dtlb_miss_write", source, context);
}

/* Data TLB protection violation (read) */
static void dtlb_pv_read(irq_t source, irq_context_t *context, void *data) {
    (void)data;
    dbgio_printf("dtlb_pv_read\n");
    unhandled_mmu(source, context);
}

/* Data TLB protection violation (write) */
static void dtlb_pv_write(irq_t source, irq_context_t *context, void *data) {
    (void)data;
    dbgio_printf("dtlb_pv_write\n");
    unhandled_mmu(source, context);
}

/* Initial page write exception */
static void initial_page_write(irq_t source, irq_context_t *context, void *data) {
    (void)data;
    dbgio_printf("initial_page_write\n");
    unhandled_mmu(source, context);
}

static const unsigned int page_mask[] = { 0x3ff, 0xfff, 0xffff, 0xfffff };

int mmu_page_map_static(uintptr_t virt, uintptr_t phys,
                        page_size_t page_size,
                        page_prot_t page_prot,
                        bool cached)
{
    unsigned int head;

    if(page_size < PAGE_SIZE_1K || page_size > PAGE_SIZE_1M ||
       page_prot < MMU_KERNEL_RDONLY || page_prot > MMU_ALL_RDWR) {
        errno = EINVAL;
        return -1;
    }

    if((virt | phys) & page_mask[page_size]) {
        errno = EINVAL;
        return -1;
    }

    /* PTEL contains a 29-bit physical address. Reject a base or page span
       that would otherwise be truncated by BUILD_PTEL(). */
    if(phys > 0x1fffffffu - page_mask[page_size]) {
        errno = EINVAL;
        return -1;
    }

    /* Entry 0 must remain available to the hardware replacement cycle. The
       two SQ translations created by mmu_init_basic() count against this cap. */
    if(tlb_nb_static >= MMU_STATIC_MAX) {
        errno = ENOSPC;
        return -1;
    }

    irq_disable_scoped();

    head = 0x3f - tlb_nb_static;

    SET_MMUCR(head, head, 1, 0, 0, 1);
    mmu_ldtlb(0, virt, phys, page_size, page_prot, cached, 1, 0, 0);
    SET_MMUCR(head - 1, 0, 1, 0, 0, 1);

    tlb_nb_static++;

    return 0;
}

void mmu_init_basic(void) {
    /* Reset number of static mappings */
    tlb_nb_static = 0;

    /* The boot environment may leave valid translations behind even though
       address translation itself is disabled. Retaining them can make the
       first KOS mapping hit an unrelated entry, so invalidate both TLBs before
       reserving the static SQ slots. TI clears itself in hardware. */
    SET_MMUCR(0, 0, 1, 0, 1, 0);
    mmu_ldtlb_wait();

    /* Reserve TLB entries 62-63 for SQ translation. Register them as read-write
     * (since there's no write-only flag) with a 1 MiB page.
     * Note that mmu_page_map_static() will enable MMU so we don't have to do it
     * later. */
    mmu_page_map_static(0xe0100000, 0, PAGE_SIZE_1M, MMU_KERNEL_RDWR, false);
    mmu_page_map_static(0xe0000000, 0, PAGE_SIZE_1M, MMU_KERNEL_RDWR, false);

    /* Clear the ITLB */
    mmu_reset_itlb();
}

/********************************************************************************/
/* Init routine */
void mmu_init(void) {
    /* Setup last URC counter (to make sure we don't thrash the
       TLB caches accidentally) */
    last_urc = 0;

    /* Set the default mapping func */
    map_func = map_virt;

    /* No context yet */
    mmu_cxt_current = NULL;

    /* No context -- shortcuts not OK yet */
    mmu_shortcut_ok = 0;

    /* Set up interrupt handlers */
    irq_set_handler(EXC_ITLB_MISS, itlb_miss, NULL);
    irq_set_handler(EXC_ITLB_PV, itlb_pv, NULL);
    irq_set_handler(EXC_DTLB_MISS_READ, dtlb_miss_read, NULL);
    irq_set_handler(EXC_DTLB_MISS_WRITE, dtlb_miss_write, NULL);
    irq_set_handler(EXC_DTLB_PV_READ, dtlb_pv_read, NULL);
    irq_set_handler(EXC_DTLB_PV_WRITE, dtlb_pv_write, NULL);
    irq_set_handler(EXC_INITIAL_PAGE_WRITE, initial_page_write, NULL);

    mmu_init_basic();
}

void mmu_shutdown_basic(void) {
    /* Turn off MMU */
    *mmucr = 0x00000204;
}

/* Shutdown */
void mmu_shutdown(void) {
    mmu_shutdown_basic();

    /* No more shortcuts */
    mmu_shortcut_ok = 0;
    mmu_cxt_current = NULL;
    map_func = NULL;

    /* Unhook the IRQ handlers */
    irq_set_handler(EXC_ITLB_MISS, NULL, NULL);
    irq_set_handler(EXC_ITLB_PV, NULL, NULL);
    irq_set_handler(EXC_DTLB_MISS_READ, NULL, NULL);
    irq_set_handler(EXC_DTLB_MISS_WRITE, NULL, NULL);
    irq_set_handler(EXC_DTLB_PV_READ, NULL, NULL);
    irq_set_handler(EXC_DTLB_PV_WRITE, NULL, NULL);
    irq_set_handler(EXC_INITIAL_PAGE_WRITE, NULL, NULL);
}

bool mmu_enabled(void) {
    return *mmucr & 0x1;
}

void mmu_set_sq_addr(void *addr) {
    uint32_t ppn1 = (uint32_t)addr & 0x1ff00000;
    uint32_t ppn2 = ppn1 + 0x00100000;

    /* Direct TLB array writes are only architecturally guaranteed from P2. */
    mmu_set_sq_addr_asm(ppn1 | 0x1fc, ppn2 | 0x1fc);
}
