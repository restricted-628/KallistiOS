/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Production table lifecycle tests; no emulated CPU or cache. */
#include <arch/mmu.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error "This test requires assertions; do not use -DNDEBUG"
#endif

static unsigned checks, allocations, fail_at, live, irq_depth;
#define CHECK(x) do { ++checks; assert(x); } while(0)
static void *test_malloc(size_t size) {
    CHECK(!irq_depth);
    if(++allocations == fail_at) { errno = ENOMEM; return NULL; }
    void *p = malloc(size);
    CHECK(p != NULL);
    ++live;
    return p;
}
static void *test_calloc(size_t n, size_t size) {
    void *p = test_malloc(n * size);
    if(p) memset(p, 0, n * size);
    return p;
}
static void test_free(void *p) {
    CHECK(!irq_depth);
    if(p) { CHECK(live > 0); --live; free(p); }
}
static void restore_irq(unsigned *old) { irq_depth = *old; }
#define irq_disable_scoped() \
    unsigned saved_irq __attribute__((cleanup(restore_irq))) = irq_depth++

typedef struct event { unsigned kind; uint32_t address, asid; } event_t;
static event_t events[4096];
static unsigned nevents;
static void record(unsigned kind, uint32_t address, uint32_t asid) {
    CHECK(irq_depth > 0);
    CHECK(nevents < sizeof(events) / sizeof(events[0]));
    events[nevents++] = (event_t){kind, address, asid};
}
void mmu_purge_phys_page(uint32_t physical) { record(0, physical, 0); }
void mmu_invalidate_tlb(uint32_t virt, uint32_t asid) { record(1, virt, asid); }
mmucontext_t *mmu_cxt_current;
int mmu_shortcut_ok;
static mmu_mapfunc_t map_func;
static mmupage_t *map_virt(mmucontext_t *, int);
static uint32_t pteh_register;
static volatile uint32_t *const pteh = &pteh_register;
static uint32_t mmucr_register;
static volatile uint32_t *const mmucr = &mmucr_register;
static unsigned tlb_nb_static, loads;
static uint32_t last_virt, last_phys;
static void mmu_ldtlb(int asid, uint32_t virt, uint32_t phys, int sz,
                      int pr, int c, int d, int sh, int wt) {
    CHECK(irq_depth > 0 && asid == 0 && d == 1 && sh == 0 && wt == 0);
    CHECK(sz >= PAGE_SIZE_1K && sz <= PAGE_SIZE_1M);
    CHECK(pr >= MMU_KERNEL_RDONLY && pr <= MMU_ALL_RDWR && (c == 0 || c == 1));
    ++loads; last_virt = virt; last_phys = phys;
}
#define PAGESIZE_BITS 12
#define malloc test_malloc
#define calloc test_calloc
#define free test_free
#include "production.inc"
#undef malloc
#undef calloc
#undef free

static void reset_events(void) { nevents = 0; allocations = 0; fail_at = 0; }
static void expect_event(unsigned index, unsigned kind, uint32_t addr, uint32_t asid) {
    CHECK(index < nevents);
    CHECK(events[index].kind == kind);
    CHECK(events[index].address == addr);
    CHECK(events[index].asid == asid);
}
static int map(mmucontext_t *c, int v, int p, int n, page_cache_t cache) {
    return mmu_page_map_ex(c, v, p, n, MMU_ALL_RDWR, cache, false, true);
}

static void allocation_rollback(void) {
    for(unsigned fail = 1; fail <= 3; ++fail) {
        reset_events();
        mmucontext_t *c = mmu_context_create(17);
        CHECK(c != NULL);
        CHECK(map(c, 511, 77, 1, MMU_CACHE_BACK) == 0);
        mmusubcontext_t before = *c->sub[0];
        mmusubcontext_t *original = c->sub[0];
        unsigned baseline = live;
        reset_events(); fail_at = fail;
        CHECK(map(c, 511, 100, 1025, MMU_CACHE_WT) == -1);
        CHECK(errno == ENOMEM);
        CHECK(live == baseline);
        CHECK(c->sub[0] == original);
        CHECK(memcmp(&before, c->sub[0], sizeof(before)) == 0);
        CHECK(!c->sub[1] && !c->sub[2]);
        CHECK(nevents == 0);
        reset_events();
        mmu_context_destroy(c);
        CHECK(live == 0);
    }
    reset_events(); fail_at = 1;
    CHECK(mmu_context_create(2) == NULL && errno == ENOMEM);
    CHECK(live == 0);
    reset_events();
}

static void lifecycle(void) {
    mmucontext_t *c = mmu_context_create(17), *peer = mmu_context_create(18);
    CHECK(c && peer);
    map_func = map_virt;
    mmu_use_table(peer); mmu_switch_context(peer);
    CHECK(mmu_shortcut_ok == 1 && pteh_register == 18);
    reset_events();
    CHECK(map(c, 511, 100, 1025, MMU_CACHE_BACK) == 0);
    CHECK(allocations == 4); /* pointer vector and three missing tables */
    CHECK(nevents == 1025);
    for(unsigned i = 0; i < 1025; ++i) {
        mmupage_t *page = map_virt(c, (int)i + 511);
        CHECK(page != NULL && page->physical == i + 100);
        CHECK(page->pteh == (i + 511) * 4096u);
        CHECK(page->ptel == ((i + 100) * 4096u | 0x17cu));
        expect_event(i, 1, (i + 511) * 4096u, 17);
    }
    CHECK(mmu_cxt_current == peer && pteh_register == 18);
    reset_events(); fail_at = 1;
    CHECK(map(c, 511, 200, 2, MMU_CACHE_WT) == 0);
    CHECK(allocations == 0); /* existing tables: no heap work */
    CHECK(nevents == 4);
    expect_event(0, 0, 100 * 4096u, 0); expect_event(1, 0, 101 * 4096u, 0);
    expect_event(2, 1, 511 * 4096u, 17); expect_event(3, 1, 512 * 4096u, 17);
    CHECK(map_virt(c, 511)->ptel == (200 * 4096u | 0x17du));
    reset_events();
    CHECK(mmu_page_set_cache(c, 511, 2, MMU_NO_CACHE) == 0);
    CHECK(nevents == 4);
    expect_event(0, 0, 200 * 4096u, 0); expect_event(1, 0, 201 * 4096u, 0);
    expect_event(2, 1, 511 * 4096u, 17); expect_event(3, 1, 512 * 4096u, 17);
    CHECK(map_virt(c, 511)->ptel == (200 * 4096u | 0x174u));
    mmupage_t before = *map_virt(c, 1535);
    reset_events();
    CHECK(mmu_page_set_cache(c, 1535, 2, MMU_NO_CACHE) == -1 && errno == ENOENT);
    CHECK(memcmp(&before, map_virt(c, 1535), sizeof(before)) == 0 && nevents == 0);
    CHECK(mmu_phys_to_virt(c, 200) == 511);
    reset_events();
    CHECK(mmu_page_unmap_ex(c, 511, 2) == 0);
    CHECK(nevents == 2 && !c->sub[0] && c->sub[1]);
    expect_event(0, 1, 511 * 4096u, 17); expect_event(1, 1, 512 * 4096u, 17);
    CHECK(mmu_virt_to_phys(c, 511) == -1);
    reset_events(); CHECK(mmu_page_unmap_ex(c, 511, 2) == 0 && nevents == 0);
    reset_events(); mmu_context_destroy(c);
    CHECK(nevents == 1023 * 2 && mmu_cxt_current == peer && pteh_register == 18);
    for(unsigned i = 0; i < 1023; ++i) {
        expect_event(i * 2, 0, (i + 102) * 4096u, 0);
        expect_event(i * 2 + 1, 1, (i + 513) * 4096u, 17);
    }
    reset_events();
    /* Active destruction and legacy source signatures. */
    void (*legacy_unmap)(mmucontext_t *, int, int) = mmu_page_unmap;
    mmu_page_map(peer, 2, 5, 1, MMU_KERNEL_RDWR, MMU_NO_CACHE, true, true);
    CHECK(map_virt(peer, 2)->shared && map_virt(peer, 2)->ptel == (5 * 4096u | 0x136u));
    legacy_unmap(peer, 2, 1);
    CHECK(!peer->sub[0]);
    mmu_context_destroy(peer);
    CHECK(!mmu_cxt_current && !mmu_shortcut_ok && live == 0 && !irq_depth);
}

static void boundaries(void) {
    reset_events();
    CHECK(!mmu_context_create(-1) && errno == EINVAL);
    CHECK(!mmu_context_create(256) && errno == EINVAL);
    mmucontext_t *c = mmu_context_create(255);
    const int bad[][3] = {{-1,0,1},{0,-1,1},{0,0,0},{0,0,-1},
        {INT_MAX,0,1},{0,INT_MAX,1},{0,0,INT_MAX},
        {524287,0,2},{0,131071,2},{524288,0,1},{0,131072,1}};
    reset_events();
    for(unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(map(c, bad[i][0], bad[i][1], bad[i][2], MMU_NO_CACHE) == -1 && errno == EINVAL);
    CHECK(map(NULL, 0, 0, 1, MMU_NO_CACHE) == -1);
    CHECK(mmu_page_map_ex(c,0,0,1,(page_prot_t)-1,MMU_NO_CACHE,false,true) == -1);
    CHECK(mmu_page_map_ex(c,0,0,1,(page_prot_t)4,MMU_NO_CACHE,false,true) == -1);
    CHECK(map(c,0,0,1,(page_cache_t)-1) == -1);
    CHECK(map(c,0,0,1,(page_cache_t)3) == -1);
    CHECK(mmu_page_unmap_ex(NULL,0,1) == -1);
    CHECK(mmu_page_unmap_ex(c,524287,2) == -1);
    CHECK(mmu_page_set_cache(c,0,1,(page_cache_t)-1) == -1);
    CHECK(mmu_page_set_cache(c,524287,2,MMU_NO_CACHE) == -1);
    CHECK(!allocations && !nevents);
    CHECK(mmu_virt_to_phys(c,INT_MAX) == -1);
    CHECK(mmu_phys_to_virt(c,-1) == -1 && mmu_phys_to_virt(c,131072) == -1);
    CHECK(map(c,524287,131071,1,MMU_CACHE_BACK) == 0);
    CHECK(mmu_virt_to_phys(c,524287) == 131071);
    CHECK(mmu_phys_to_virt(c,131071) == 524287);
    mmu_context_destroy(c); mmu_context_destroy(NULL);
    CHECK(!live && !irq_depth);
}

static void static_maps(void) {
    tlb_nb_static = 0; loads = 0; mmucr_register = 0x12345678;
    CHECK(mmu_page_map_static(0,0,(page_size_t)-1,MMU_KERNEL_RDWR,false) == -1);
    CHECK(mmu_page_map_static(0,0,(page_size_t)4,MMU_KERNEL_RDWR,false) == -1);
    CHECK(mmu_page_map_static(0,0,PAGE_SIZE_4K,(page_prot_t)-1,false) == -1);
    CHECK(mmu_page_map_static(0,0,PAGE_SIZE_4K,(page_prot_t)4,false) == -1);
    for(unsigned sz = 0; sz < 4; ++sz) {
        CHECK(mmu_page_map_static(1,0,(page_size_t)sz,MMU_KERNEL_RDWR,false) == -1);
        CHECK(mmu_page_map_static(0,1,(page_size_t)sz,MMU_KERNEL_RDWR,false) == -1);
        CHECK(mmu_page_map_static(1,1,(page_size_t)sz,MMU_KERNEL_RDWR,false) == -1);
        CHECK(mmu_page_map_static(0,0x20000000u,(page_size_t)sz,MMU_KERNEL_RDWR,false) == -1);
    }
    CHECK(loads == 0 && tlb_nb_static == 0 && mmucr_register == 0x12345678);
    for(unsigned i = 0; i < 62; ++i) {
        unsigned sz = i % 4;
        uint32_t physical = 0x1fffffffu - page_mask[sz];
        CHECK(mmu_page_map_static(i * 0x100000u,physical,(page_size_t)sz,
                                  MMU_KERNEL_RDWR,true) == 0);
        CHECK(loads == i + 1 && tlb_nb_static == i + 1);
        CHECK(last_virt == i * 0x100000u && last_phys == physical);
        CHECK(mmucr_register == ((62u - i) << 18 | 0x201u));
    }
    uint32_t before = mmucr_register;
    CHECK(mmu_page_map_static(0,0,PAGE_SIZE_4K,MMU_KERNEL_RDWR,false) == -1);
    CHECK(errno == ENOSPC && loads == 62 && tlb_nb_static == 62);
    CHECK(mmucr_register == before && !irq_depth);
}

int main(void) {
    allocation_rollback(); lifecycle(); boundaries(); static_maps();
    printf("MMU-PAGE: PASS checks=%u rollback=4 lifecycle=4 legacy=2 static=62\n", checks);
    return 0;
}
