/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <arch/mmu.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error Assertions are required
#endif

static atomic_uint checks;
#define CHECK(x) do { atomic_fetch_add(&checks, 1); assert(x); } while(0)
static _Thread_local uint32_t test_sr;
static _Thread_local int test_irq, fail_lock;
static _Thread_local unsigned irq_depth;
static unsigned attempts, fail_at, live, map_calls, purges, invalidations;
static void *watched_mutex;
static atomic_uint mutex_waiters;
typedef struct {
    void *p;
    bool backing, prepared, mapped, retired_cache, retired_tlb;
} allocation_t;
static allocation_t allocations[128];
/* Deterministic host backing geometry; never changes VM control flow. */
static unsigned char *backing_pool;
static unsigned pool_order[8], pool_next;
static void (*allocation_inject)(void);
static mmucontext_t *contexts[8];
static void verify_free(allocation_t *a);
static allocation_t *allocation(void *p) {
    for(unsigned i = 0; i < 128; ++i)
        if(allocations[i].p == p) return &allocations[i];
    CHECK(false);
    return NULL;
}
static allocation_t *physical(unsigned p) {
    for(unsigned i = 0; i < 128; ++i)
        if(allocations[i].p && allocations[i].backing &&
           (((uintptr_t)allocations[i].p & 0x1fffffff) >> 12) == p)
            return &allocations[i];
    CHECK(false);
    return NULL;
}
static void *allocate(size_t bytes, bool aligned) {
    CHECK(!irq_depth);
    if(allocation_inject) {
        void (*fn)(void) = allocation_inject;
        allocation_inject = NULL;
        fn();
    }
    if(++attempts == fail_at) { errno = ENOMEM; return NULL; }
    void *p;
    if(aligned && backing_pool) {
        CHECK(pool_next < 8);
        p = backing_pool + pool_order[pool_next++] * 4096;
    }
    else p = aligned ? aligned_alloc(4096, bytes) : malloc(bytes);
    CHECK(p);
    for(unsigned i = 0; i < 128; ++i) {
        if(!allocations[i].p) {
            allocations[i] = (allocation_t){.p = p, .backing = aligned};
            memset(p, 0xa5, bytes);
            ++live;
            return p;
        }
    }
    CHECK(false);
    return NULL;
}
static void *test_malloc(size_t n) { return allocate(n, false); }
static void *test_calloc(size_t n, size_t size) {
    CHECK(size == 0 || n <= SIZE_MAX / size);
    void *p = allocate(n * size, false);
    if(p) memset(p, 0, n * size);
    return p;
}
static void *test_aligned_alloc(size_t align, size_t n) {
    CHECK(align == 4096 && n == 4096);
    return allocate(n, true);
}
static void test_free(void *p) {
    CHECK(!irq_depth);
    if(!p) return;
    allocation_t *a = allocation(p);
    if(a->backing) verify_free(a);
    *a = (allocation_t){0};
    CHECK(live);
    --live;
    if(!backing_pool || (uintptr_t)p < (uintptr_t)backing_pool ||
       (uintptr_t)p >= (uintptr_t)backing_pool + 8 * 4096)
        free(p);
}
/* Inject an interrupt at the platform exclusion boundary, without replacing
   production VM control flow. Used only by the single-thread query fixture. */
static void (*irq_inject)(void);
static unsigned irq_countdown;
static unsigned disable_irq(void) {
    if(!irq_depth && irq_inject && --irq_countdown == 0) {
        void (*fn)(void) = irq_inject;
        irq_inject = NULL;
        fn();
    }
    return irq_depth++;
}
static void restore_irq(unsigned *old) { irq_depth = *old; }
#define irq_disable_scoped() unsigned old_irq __attribute__((cleanup(restore_irq))) = disable_irq()
static mmupage_t *map_virt(mmucontext_t *, int);
void mmu_purge_phys_page(uint32_t p) {
    CHECK(irq_depth);
    ++purges;
    physical(p >> 12)->retired_cache = true;
}
void mmu_invalidate_tlb(uint32_t v, uint32_t asid) {
    CHECK(irq_depth);
    ++invalidations;
    for(unsigned i = 0; i < 8; ++i) {
        if(contexts[i] && (unsigned)contexts[i]->asid == asid) {
            mmupage_t *p = map_virt(contexts[i], v >> 12);
            if(p) {
                allocation_t *a = physical(p->physical);
                CHECK(a->retired_cache);
                a->retired_tlb = true;
            }
        }
    }
}
mmucontext_t *mmu_cxt_current;
int mmu_shortcut_ok;
static mmu_mapfunc_t map_func;
static uint32_t pteh_value;
static volatile uint32_t *const pteh = &pteh_value;
#define PAGESIZE_BITS 12
#include "vm-internal.h"
#define malloc test_malloc
#define calloc test_calloc
#define free test_free
#include "mmu.inc"
#undef malloc
#undef calloc
#undef free

static void verify_free(allocation_t *a) {
    if(a->mapped) CHECK(a->retired_cache && a->retired_tlb);
    unsigned phys = ((uintptr_t)a->p & 0x1fffffff) >> 12;
    for(unsigned k = 0; k < 8; ++k) {
        if(!contexts[k]) continue;
        for(unsigned t = 0; t < MMU_PAGES; ++t) {
            if(!contexts[k]->sub[t]) continue;
            for(unsigned p = 0; p < MMU_SUB_PAGES; ++p) {
                mmupage_t *page = &contexts[k]->sub[t]->page[p];
                CHECK(!page->valid || page->physical != phys);
            }
        }
    }
}
void dcache_purge_range(uintptr_t p, size_t n) {
    CHECK(!irq_depth && n == 4096);
    allocation_t *a = allocation((void *)p);
    CHECK(a->backing && !a->mapped);
    for(size_t i = 0; i < n; ++i) CHECK(((unsigned char *)p)[i] == 0);
    a->prepared = true;
}
static int checked_map(mmucontext_t *c, int v, int p, int n, page_prot_t prot,
                       page_cache_t cache, bool shared, bool dirty) {
    allocation_t *a = physical(p);
    CHECK(a->prepared && n == 1 && !shared && dirty && cache == MMU_CACHE_BACK);
    CHECK(prot == MMU_KERNEL_RDONLY || prot == MMU_KERNEL_RDWR);
    ++map_calls;
    int rc = mmu_page_map_ex(c, v, p, n, prot, cache, shared, dirty);
    if(!rc) {
        a->mapped = true;
        a->retired_cache = a->retired_tlb = false;
    }
    return rc;
}
#define malloc test_malloc
#define calloc test_calloc
#define free test_free
#define aligned_alloc test_aligned_alloc
#define mmu_page_map_ex checked_map
#include "vm.inc"
#undef malloc
#undef calloc
#undef free
#undef aligned_alloc
#undef mmu_page_map_ex

#define P(x) ((void *)(uintptr_t)(x))
#define BASE 0x101ff000u
static mmucontext_t *context(unsigned i) {
    contexts[i] = mmu_context_create(30 + i);
    CHECK(contexts[i]);
    return contexts[i];
}
static void drop(unsigned i) {
    mmu_context_destroy(contexts[i]);
    contexts[i] = NULL;
}
static void reset_failure(void) { attempts = fail_at = 0; }
static void expect(vm_space_t *s, uintptr_t a, vm_state_t state, size_t bytes, unsigned prot) {
    vm_info_t out;
    CHECK(vm_query(s, P(a), &out) == 0);
    CHECK(out.state == state && out.run_size == bytes && out.protection == prot);
}

static void bounds(void) {
    mmucontext_t *c = context(0);
    uintptr_t bad[] = {0, 1, 0xfff, 0x7c000000, 0x7ffff000, 0x80000000, UINTPTR_MAX};
    for(unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        CHECK(!vm_space_create(c, P(bad[i]), 4096) && errno == EINVAL);
    }
    CHECK(!vm_space_create(c, P(0x7bfff000), 8192) && errno == EINVAL);
    CHECK(!vm_space_create(c, P(BASE), SIZE_MAX) && errno == EINVAL);
    CHECK(!vm_space_create(c, P(BASE), 0) && errno == EINVAL);
    CHECK(!vm_space_create(NULL, P(BASE), 4096) && errno == EINVAL);
    c->asid = -1;
    CHECK(!vm_space_create(c, P(BASE), 4096) && errno == EINVAL);
    c->asid = 256;
    CHECK(!vm_space_create(c, P(BASE), 4096) && errno == EINVAL);
    c->asid = 30;
    vm_space_t *s = vm_space_create(c, P(0x7bfff000), 4096);
    CHECK(s);
    void *out = P(123);
    CHECK(!vm_reserve(s, NULL, 4096, &out) && out == P(0x7bfff000));
    CHECK(!vm_commit(s, out, 4096, VM_READWRITE));
    CHECK(!vm_release(s, out));
    CHECK(!vm_space_destroy(s));
    reset_failure(); fail_at = 1;
    CHECK(!vm_space_create(c, P(BASE), 4096) && errno == ENOMEM);
    reset_failure();
    drop(0);
    CHECK(!live);
}

static void lifetime(void) {
    mmucontext_t *c = context(0);
    vm_space_t *s = vm_space_create(c, P(BASE), 8 * 4096);
    CHECK(s);
    void *out = P(123);
    CHECK(!vm_reserve(s, P(BASE + 4096), 3 * 4096, &out));
    CHECK(out == P(BASE + 4096));
    CHECK(vm_reserve(s, out, 4096, &out) < 0 && errno == EEXIST);
    CHECK(out == P(BASE + 4096));
    expect(s, BASE, VM_FREE, 4096, 0);
    expect(s, BASE + 4097, VM_RESERVED, 3 * 4096, 0);
    CHECK(vm_space_destroy(s) < 0 && errno == EBUSY);
    CHECK(vm_commit(s, P(BASE), 4096, VM_READ) < 0 && errno == ENOENT);
    CHECK(vm_commit(s, out, 4 * 4096, VM_READ) < 0 && errno == EINVAL);
    CHECK(vm_commit(s, out, 4096, 0) < 0 && errno == ENOTSUP);
    CHECK(!vm_commit(s, out, 4096, VM_READWRITE));
    vm_info_t snapshot;
    CHECK(!vm_query(s, out, &snapshot));
    CHECK(vm_commit(s, out, 2 * 4096, VM_READ) < 0 && errno == EALREADY);
    CHECK(vm_protect(s, out, 3 * 4096, VM_READ) < 0 && errno == ENOENT);
    unsigned calls = map_calls;
    CHECK(!vm_protect(s, out, 4096, VM_READWRITE) && map_calls == calls);
    CHECK(!vm_protect(s, out, 4096, VM_READ));
    CHECK(snapshot.protection == VM_READWRITE);
    mmupage_t *pte = map_virt(c, (BASE + 4096) >> 12);
    CHECK(pte && pte->prkey == MMU_KERNEL_RDONLY);
    CHECK(!vm_decommit(s, out, 3 * 4096));
    CHECK(!vm_decommit(s, out, 3 * 4096));
    CHECK(!vm_commit(s, out, 3 * 4096, VM_READWRITE));
    CHECK(vm_release(s, P(BASE + 2 * 4096)) < 0 && errno == ENOENT);
    CHECK(!vm_release(s, out));
    CHECK(vm_release(s, out) < 0 && errno == ENOENT);
    expect(s, BASE + 2, VM_FREE, 8 * 4096, 0);
    CHECK(!vm_reserve(s, NULL, 8 * 4096, &out) && out == P(BASE));
    CHECK(vm_reserve(s, NULL, 4096, &out) < 0 && errno == ENOMEM);
    CHECK(!vm_release(s, out));
    CHECK(!vm_space_destroy(s));
    drop(0);
    CHECK(!live);
}

static void failures(void) {
    /* Three backing pages and two new second-level tables (two allocations each).
       Starting at the last page in a table exercises rollback across the split. */
    for(unsigned f = 1; f <= 8; ++f) {
        mmucontext_t *c = context(0);
        vm_space_t *s = vm_space_create(c, P(BASE), 4 * 4096);
        CHECK(s);
        void *out = P(123);
        unsigned before = live;
        reset_failure(); fail_at = 1;
        CHECK(vm_reserve(s, NULL, 3 * 4096, &out) < 0 && errno == ENOMEM);
        CHECK(out == P(123) && live == before);
        reset_failure();
        CHECK(!vm_reserve(s, NULL, 3 * 4096, &out));
        before = live;
        reset_failure(); fail_at = f;
        int rc = vm_commit(s, out, 3 * 4096, VM_READWRITE);
        if(f <= 7) {
            CHECK(rc < 0 && errno == ENOMEM && live == before);
            for(unsigned i = 0; i < 3; ++i)
                CHECK(mmu_virt_to_phys(c, (BASE >> 12) + i) == -1);
            expect(s, BASE, VM_RESERVED, 3 * 4096, 0);
        }
        else CHECK(rc == 0);
        reset_failure();
        CHECK(!vm_release(s, out));
        CHECK(!vm_space_destroy(s));
        drop(0);
        CHECK(!live);
    }
}

static void callers(void) {
    mmucontext_t *c = context(0);
    vm_space_t *s = vm_space_create(c, P(BASE), 4096);
    CHECK(s);
    void *out = P(123);
    vm_info_t info, before;
    memset(&info, 0x51, sizeof(info)); before = info;
    uint32_t masks[] = {0x10, 0x20, 0x40, 0x80, 0xf0, 0x10000000, 0};
    for(unsigned i = 0; i < 7; ++i) {
        test_sr = masks[i]; test_irq = i == 6;
        unsigned count = attempts;
        CHECK(!vm_space_create(c, P(BASE), 4096) && errno == EPERM);
        CHECK(vm_reserve(s, NULL, 4096, &out) < 0 && errno == EPERM);
        CHECK(vm_commit(s, P(BASE), 4096, VM_READ) < 0 && errno == EPERM);
        CHECK(vm_protect(s, P(BASE), 4096, VM_READ) < 0 && errno == EPERM);
        CHECK(vm_decommit(s, P(BASE), 4096) < 0 && errno == EPERM);
        CHECK(vm_release(s, P(BASE)) < 0 && errno == EPERM);
        CHECK(vm_query(s, P(BASE), &info) < 0 && errno == EPERM);
        CHECK(vm_space_destroy(s) < 0 && errno == EPERM);
        CHECK(attempts == count && out == P(123) && !memcmp(&info, &before, sizeof(info)));
    }
    test_sr = 0; test_irq = 0; fail_lock = 1;
    CHECK(vm_query(s, P(BASE), &info) < 0 && errno == EAGAIN);
    CHECK(!memcmp(&info, &before, sizeof(info)));
    fail_lock = 0;
    CHECK(vm_query(s, P(BASE + 4096), &info) < 0 && errno == EINVAL);
    CHECK(!vm_space_destroy(s)); drop(0); CHECK(!live);
}

static void ranges_and_runs(void) {
    vm_space_t *s = vm_space_create(context(0), P(0x1000), 6 * 4096);
    CHECK(s);
    void *a, *b, *sentinel = P(99);
    CHECK(!vm_reserve(s, P(0x3000), 2 * 4096, &a));
    CHECK(!vm_reserve(s, NULL, 2 * 4096, &b) && b == P(0x1000));
    CHECK(vm_commit(s, b, 3 * 4096, VM_READ) < 0 && errno == EINVAL);
    CHECK(vm_reserve(s, P(0x6000), 2 * 4096, &sentinel) < 0 && errno == EINVAL);
    CHECK(sentinel == P(99));
    size_t bad_size[] = {0, 1, 4095, 4097, SIZE_MAX, SIZE_MAX - 4095};
    for(unsigned i = 0; i < sizeof(bad_size) / sizeof(*bad_size); ++i) {
        CHECK(vm_commit(s, a, bad_size[i], VM_READ) < 0 && errno == EINVAL);
        CHECK(vm_decommit(s, a, bad_size[i]) < 0 && errno == EINVAL);
    }
    CHECK(vm_protect(s, P(0x3001), 4096, VM_READ) < 0 && errno == EINVAL);
    CHECK(vm_protect(s, a, 4096, 2) < 0 && errno == ENOTSUP);
    CHECK(!vm_commit(s, a, 2 * 4096, VM_READWRITE));
    mmupage_t old = *map_virt(contexts[0], 3);
    unsigned char *contents = physical(old.physical)->p;
    contents[73] = 0x67; /* Host stand-in for a translated CPU store. */
    CHECK(!vm_protect(s, a, 4096, VM_READ));
    CHECK(contents[73] == 0x67 && map_virt(contexts[0], 3)->physical == old.physical);
    vm_info_t info;
    CHECK(!vm_query(s, P(0x4001), &info));
    CHECK(info.run_base == P(0x4000) && info.run_size == 4096 && info.region_base == a);
    CHECK(!vm_query(s, P(0x2001), &info));
    CHECK(info.region_base == b && info.run_size == 8192);
    CHECK(!vm_query(s, P(0x6001), &info));
    CHECK(info.run_base == P(0x5000) && info.run_size == 8192 && info.region_size == 0);
    unsigned before = attempts;
    fail_lock = 1;
    CHECK(vm_reserve(s, NULL, 4096, &sentinel) < 0 && errno == EAGAIN);
    CHECK(vm_commit(s, b, 4096, VM_READ) < 0 && errno == EAGAIN);
    CHECK(vm_protect(s, a, 4096, VM_READWRITE) < 0 && errno == EAGAIN);
    CHECK(vm_decommit(s, a, 4096) < 0 && errno == EAGAIN);
    CHECK(vm_release(s, a) < 0 && errno == EAGAIN);
    CHECK(vm_space_destroy(s) < 0 && errno == EAGAIN);
    CHECK(attempts == before && sentinel == P(99));
    fail_lock = 0;
    CHECK(!vm_release(s, b) && !vm_release(s, a));
    CHECK(!vm_space_destroy(s)); drop(0); CHECK(!live);
}

static void rollback_preserves_neighbor(void) {
    for(unsigned f = 1; f <= 6; ++f) {
        mmucontext_t *c = context(0);
        vm_space_t *s = vm_space_create(c, P(BASE), 4 * 4096);
        void *neighbor, *range;
        CHECK(s);
        CHECK(!vm_reserve(s, P(BASE + 3 * 4096), 4096, &neighbor));
        CHECK(!vm_commit(s, neighbor, 4096, VM_READ));
        mmupage_t original = *map_virt(c, (BASE >> 12) + 3);
        unsigned char *contents = physical(original.physical)->p;
        contents[0] = 0x98;
        CHECK(!vm_reserve(s, P(BASE), 3 * 4096, &range));
        unsigned before = live;
        reset_failure(); fail_at = f;
        int rc = vm_commit(s, range, 3 * 4096, VM_READWRITE);
        if(f <= 5) CHECK(rc < 0 && errno == ENOMEM && live == before);
        else CHECK(!rc);
        CHECK(!memcmp(&original, map_virt(c, (BASE >> 12) + 3), sizeof(original)));
        CHECK(contents[0] == 0x98);
        reset_failure();
        CHECK(!vm_release(s, range) && !vm_release(s, neighbor));
        CHECK(!vm_space_destroy(s)); drop(0); CHECK(!live);
    }
}

static void separation(void) {
    mmucontext_t *a = context(0), *b = context(1);
    vm_space_t *sa = vm_space_create(a, P(BASE), 4096);
    vm_space_t *sb = vm_space_create(b, P(BASE), 4096);
    void *va, *vb;
    CHECK(sa && sb);
    CHECK(!vm_reserve(sa, NULL, 4096, &va) && !vm_reserve(sb, NULL, 4096, &vb));
    CHECK(!vm_commit(sa, va, 4096, VM_READWRITE) && !vm_commit(sb, vb, 4096, VM_READ));
    CHECK(mmu_virt_to_phys(a, BASE >> 12) != mmu_virt_to_phys(b, BASE >> 12));
    mmu_use_table(b);
    CHECK(!vm_release(sa, va));
    CHECK(mmu_cxt_current == b && map_virt(b, BASE >> 12));
    CHECK(!vm_release(sb, vb));
    CHECK(!vm_space_destroy(sa) && !vm_space_destroy(sb));
    drop(0); drop(1); CHECK(!live);
}

static void preexisting_mapping(void) {
    mmucontext_t *c = context(0);
    void *backing = test_aligned_alloc(4096, 4096);
    memset(backing, 0, 4096);
    dcache_purge_range((uintptr_t)backing, 4096);
    CHECK(!checked_map(c, BASE >> 12,
          ((uintptr_t)backing & 0x1fffffff) >> 12, 1,
          MMU_KERNEL_RDWR, MMU_CACHE_BACK, false, true));
    unsigned before = live;
    CHECK(!vm_space_create(c, P(BASE), 4096) && errno == EEXIST);
    CHECK(live == before && map_virt(c, BASE >> 12));
    /* A mapping outside the borrowed arena is allowed and left untouched. */
    vm_space_t *s = vm_space_create(c, P(BASE + 4096), 4096);
    CHECK(s && !vm_space_destroy(s) && map_virt(c, BASE >> 12));
    CHECK(!mmu_page_unmap_ex(c, BASE >> 12, 1));
    test_free(backing);
    drop(0); CHECK(!live);
}

static void *contender(void *arg) {
    vm_space_t *s = arg;
    for(unsigned i = 0; i < 80; ++i) {
        void *out;
        CHECK(!vm_reserve(s, NULL, 4096, &out));
        CHECK(!vm_commit(s, out, 4096, VM_READWRITE));
        vm_info_t info;
        CHECK(!vm_query(s, out, &info) && info.region_base == out);
        CHECK(!vm_protect(s, out, 4096, VM_READ));
        CHECK(!vm_release(s, out));
    }
    return NULL;
}
static void serialization(void) {
    vm_space_t *s = vm_space_create(context(0), P(BASE), 8 * 4096);
    CHECK(s);
    pthread_t threads[4];
    for(unsigned i = 0; i < 4; ++i) CHECK(!pthread_create(&threads[i], NULL, contender, s));
    for(unsigned i = 0; i < 4; ++i) CHECK(!pthread_join(threads[i], NULL));
    CHECK(!vm_space_destroy(s)); drop(0); CHECK(!live);
}

static irq_context_t fault_frame(void) {
    irq_context_t f;
    memset(&f, 0x39, sizeof(f));
    f.pc = 0x8c001000; f.sr = 0x40000001; f.r[15] = 0x8c345000;
    return f;
}
/* Publish tracking models the hardware mapping side effect only. */
static mmupage_t *resolve(vm_space_t *s, irq_context_t *f, unsigned event,
                          uint32_t address, unsigned asid, unsigned control) {
    unsigned before = attempts;
    int saved = errno;
    irq_context_t copy = *f;
    ++irq_depth;
    mmupage_t *p = vm_fault_resolve(s->context, event, f, address, asid, control, 0x8c123400);
    --irq_depth;
    CHECK(attempts == before && errno == saved && !memcmp(f, &copy, sizeof(copy)));
    if(p) {
        allocation_t *a = physical(p->physical);
        a->mapped = true; a->retired_cache = a->retired_tlb = false;
    }
    return p;
}
static mmupage_t *resolve_ok(vm_space_t *s, vm_fault_t *t, unsigned event) {
    irq_context_t f = fault_frame();
    mmupage_t *p = resolve(s, &f, event, (uintptr_t)t->info.page + 28,
                          s->context->asid, 1);
    CHECK(p);
    vm_fault_info_t i;
    CHECK(!vm_fault_query(t, &i));
    CHECK(i.state == VM_FAULT_RESOLVED && i.snapshot_valid);
    CHECK(i.snapshot.pc == f.pc && i.snapshot.sr == f.sr && i.snapshot.sp == f.r[15]);
    CHECK(i.snapshot.address == (uintptr_t)i.page + 28 && i.snapshot.event == event);
    CHECK(i.snapshot.table == (uintptr_t)s->context && i.snapshot.carrier == 0x8c123400);
    CHECK(i.snapshot.asid == (unsigned)s->context->asid && i.snapshot.mmucr == 1);
    CHECK(i.snapshot.outcome == VM_FAULT_RETRY && i.snapshot.classification == VM_FAULT_MANAGED);
    CHECK(i.snapshot.access == (event == 0x60 ? VM_FAULT_WRITE : VM_FAULT_READ_OR_FETCH));
    CHECK(!resolve(s, &f, event, (uintptr_t)i.page, s->context->asid, 1));
    return p;
}
static void busy_ticket(vm_space_t *s, void *page) {
    vm_fault_t *sentinel = (void *)123;
    unsigned before = attempts;
    CHECK(vm_fault_arm(s, page, VM_FAULT_GUARD, VM_READWRITE, &sentinel) < 0 && errno == EBUSY);
    CHECK(sentinel == (void *)123);
    CHECK(vm_commit(s, page, 4096, VM_READWRITE) < 0 && errno == EBUSY);
    CHECK(vm_protect(s, page, 4096, VM_READ) < 0 && errno == EBUSY);
    CHECK(vm_decommit(s, page, 4096) < 0 && errno == EBUSY);
    CHECK(vm_release(s, P(BASE)) < 0 && errno == EBUSY);
    CHECK(vm_space_destroy(s) < 0 && errno == EBUSY);
    CHECK(attempts == before);
}
static void fault_lifetime(void) {
    for(unsigned guard = 0; guard < 2; ++guard) {
        for(unsigned resolved = 0; resolved < 2; ++resolved) {
            mmucontext_t *c = context(0); mmu_use_table(c);
            vm_space_t *s = vm_space_create(c, P(BASE), 4 * 4096);
            void *v; vm_fault_t *t;
            CHECK(s && !vm_reserve(s, P(BASE), 4 * 4096, &v));
            CHECK(!vm_space_create(c, P(BASE + 8 * 4096), 4096) && errno == EEXIST);
            if(guard) {
                CHECK(!vm_commit(s, v, 4096, VM_READWRITE));
                ((unsigned char *)s->regions->pages[0].backing)[73] = 0x67;
            }
            CHECK(!vm_fault_arm(s, v, guard ? VM_FAULT_GUARD : VM_FAULT_ZERO_COMMIT, VM_READWRITE, &t));
            CHECK(t->info.generation == 1 && !t->info.snapshot_valid);
            void *backing = t->backing;
            expect(s, BASE, guard ? VM_GUARDED : VM_RESERVED, guard ? 4096 : 4 * 4096, guard ? VM_READWRITE : 0);
            CHECK(!map_virt(c, BASE >> 12));
            busy_ticket(s, v);
            if(resolved) {
                CHECK(resolve_ok(s, t, 0x60)->physical == (((uintptr_t)backing & 0x1fffffff) >> 12));
                expect(s, BASE, VM_COMMITTED, 4096, VM_READWRITE);
                busy_ticket(s, v);
                if(!guard) for(unsigned i = 0; i < 4096; ++i) CHECK(!((unsigned char *)backing)[i]);
            }
            if(guard) CHECK(((unsigned char *)backing)[73] == 0x67);
            CHECK(!vm_fault_release(t));
            expect(s, BASE, (guard || resolved) ? VM_COMMITTED : VM_RESERVED,
                   (guard || resolved) ? 4096 : 4 * 4096, (guard || resolved) ? VM_READWRITE : 0);
            if(guard || resolved) CHECK(s->regions->pages[0].backing == backing);
            else CHECK(!c->sub[(BASE >> 12) / MMU_SUB_PAGES]);
            CHECK(!s->ticket_count && !s->faults[(BASE >> 12) / MMU_SUB_PAGES]);
            CHECK(!vm_release(s, v) && !vm_space_destroy(s)); drop(0); CHECK(!live);
        }
    }
}
static void fault_failures(void) {
    for(unsigned guard = 0; guard < 2; ++guard) {
        /* ZERO: descriptor, backing, lookup, MMU table; GUARD: descriptor, lookup. */
        unsigned count = guard ? 2 : 4;
        for(unsigned f = 1; f <= count + 1; ++f) {
            mmucontext_t *c = context(0);
            vm_space_t *s = vm_space_create(c, P(BASE), 4096);
            void *v; vm_fault_t *t = (void *)123;
            CHECK(s && !vm_reserve(s, NULL, 4096, &v));
            if(guard) CHECK(!vm_commit(s, v, 4096, VM_READ));
            mmupage_t old = guard ? *map_virt(c, BASE >> 12) : (mmupage_t){0};
            unsigned before = live;
            reset_failure(); fail_at = f;
            int rc = vm_fault_arm(s, v, guard ? VM_FAULT_GUARD : VM_FAULT_ZERO_COMMIT, VM_READ, &t);
            if(f <= count) {
                CHECK(rc < 0 && errno == ENOMEM && live == before && t == (void *)123);
                CHECK(!s->generation && !s->ticket_count);
                if(guard) CHECK(!memcmp(&old, map_virt(c, BASE >> 12), sizeof(old)));
                else CHECK(!c->sub[(BASE >> 12) / MMU_SUB_PAGES]);
            }
            else CHECK(!rc && !vm_fault_release(t));
            reset_failure(); CHECK(!vm_release(s, v) && !vm_space_destroy(s));
            drop(0); CHECK(!live);
        }
    }
}
static void fault_admission(void) {
    mmucontext_t *c = context(0), *other = context(1); mmu_use_table(c);
    vm_space_t *s = vm_space_create(c, P(BASE), 4096);
    void *v; vm_fault_t *t;
    CHECK(s && !vm_reserve(s, NULL, 4096, &v));
    CHECK(vm_fault_arm(s, v, VM_FAULT_GUARD, VM_READ, &t) < 0 && errno == EALREADY);
    CHECK(vm_fault_arm(s, P(BASE + 1), VM_FAULT_ZERO_COMMIT, VM_READ, &t) < 0 && errno == EINVAL);
    CHECK(vm_fault_arm(s, v, 42, VM_READ, &t) < 0 && errno == EINVAL);
    CHECK(!vm_fault_arm(s, v, VM_FAULT_ZERO_COMMIT, VM_READ, &t));
    irq_context_t f = fault_frame();
    const unsigned bad_sr[] = {0, 0x20000000, 0x50000000, 0x60000000, 0x70000000,
                              0x40000010, 0x40000020, 0x40000040, 0x40000080, 0x400000f0};
    for(unsigned i = 0; i < sizeof(bad_sr)/sizeof(*bad_sr); ++i) {
        f.sr = bad_sr[i]; CHECK(!resolve(s, &f, 0x40, BASE, c->asid, 1));
    }
    f = fault_frame();
    for(uint64_t sp = 0xffffff80; sp <= UINT32_MAX; ++sp) {
        f.r[15] = sp; f.r[0] = f.pc; /* Includes the gUSA region-end window. */
        CHECK(!resolve(s, &f, 0x40, BASE, c->asid, 1));
    }
    const unsigned bad_direct[] = {0, 0x7ffffffc, 0xa0000000, 0xffffffff, 0x8c001003};
    for(unsigned i = 0; i < sizeof(bad_direct)/sizeof(*bad_direct); ++i) {
        f = fault_frame(); f.pc = bad_direct[i]; CHECK(!resolve(s, &f, 0x40, BASE, c->asid, 1));
        f = fault_frame(); f.r[15] = bad_direct[i]; CHECK(!resolve(s, &f, 0x40, BASE, c->asid, 1));
    }
    f = fault_frame();
    const unsigned events[] = {0, 0x60, 0x80, 0xa0, 0xc0, 0xe0, 0x100, 0x140};
    for(unsigned i = 0; i < sizeof(events)/sizeof(*events); ++i)
        CHECK(!resolve(s, &f, events[i], BASE, c->asid, 1));
    CHECK(!resolve(s, &f, 0x40, BASE, other->asid, 1));
    CHECK(!resolve(s, &f, 0x40, BASE, c->asid, 0));
    CHECK(!resolve(s, &f, 0x40, BASE - 1, c->asid, 1));
    CHECK(!resolve(s, &f, 0x40, BASE + 4096, c->asid, 1));
    mmu_use_table(other); CHECK(!resolve(s, &f, 0x40, BASE, c->asid, 1)); mmu_use_table(c);
    vm_spaces[c->asid] = NULL; CHECK(!resolve(s, &f, 0x40, BASE, c->asid, 1)); vm_spaces[c->asid] = s;
    mmusubcontext_t *sub = c->sub[(BASE >> 12)/MMU_SUB_PAGES];
    mmupage_t *p = &sub->page[(BASE >> 12)%MMU_SUB_PAGES];
    p->valid = 1; CHECK(!resolve(s, &f, 0x40, BASE, c->asid, 1)); p->valid = 0;
    p->blank = 0; CHECK(!resolve(s, &f, 0x40, BASE, c->asid, 1)); p->blank = MMU_VM_PIN;
    c->sub[(BASE >> 12)/MMU_SUB_PAGES] = NULL;
    CHECK(!resolve(s, &f, 0x40, BASE, c->asid, 1)); c->sub[(BASE >> 12)/MMU_SUB_PAGES] = sub;
    t->prepared.valid = 0; CHECK(!resolve(s, &f, 0x40, BASE, c->asid, 1)); t->prepared.valid = 1;
    t->prepared.ptel &= ~0x100u; CHECK(!resolve(s, &f, 0x40, BASE, c->asid, 1)); t->prepared.ptel |= 0x100;
    CHECK(t->info.state == VM_FAULT_ARMED && !t->info.snapshot_valid && !s->sequence);
    CHECK(resolve_ok(s, t, 0x40)); CHECK(s->sequence == 1);
    CHECK(!vm_fault_release(t));
    CHECK(vm_fault_arm(s, v, VM_FAULT_ZERO_COMMIT, VM_READ, &t) < 0 && errno == EALREADY);
    CHECK(vm_fault_arm(s, v, VM_FAULT_GUARD, VM_READWRITE, &t) < 0 && errno == EINVAL);
    s->generation = UINT64_MAX;
    CHECK(vm_fault_arm(s, v, VM_FAULT_GUARD, VM_READ, &t) < 0 && errno == EOVERFLOW);
    CHECK(!vm_release(s, v) && !vm_space_destroy(s)); drop(0); drop(1); CHECK(!live);
}
static vm_space_t *inject_space;
static vm_fault_t *inject_tickets[2];
static void interrupt_scan(void) {
    irq_context_t f = fault_frame();
    for(unsigned i = 0; i < 2; ++i)
        CHECK(resolve(inject_space, &f, 0x40, (uintptr_t)inject_tickets[i]->info.page,
                      inject_space->context->asid, 1));
}
static void fault_query_and_pins(void) {
  for(unsigned guard = 0; guard < 2; ++guard) {
    mmucontext_t *c = context(0); mmu_use_table(c);
    /* Start after the 512-page boundary so all pages share one MMU table. */
    uintptr_t base = BASE + 4096;
    vm_space_t *s = vm_space_create(c, P(base), 4 * 4096);
    void *v; vm_fault_t *t[2]; CHECK(s && !vm_reserve(s, NULL, 4 * 4096, &v));
    CHECK(!vm_commit(s, P(base + 8192), 4096, VM_READ));
    if(guard) CHECK(!vm_commit(s, v, 8192, VM_READ));
    for(unsigned i = 0; i < 2; ++i) CHECK(!vm_fault_arm(s, P(base + i*4096), guard ? VM_FAULT_GUARD : VM_FAULT_ZERO_COMMIT, VM_READ, &t[i]));
    mmusubcontext_t *sub = c->sub[(base >> 12)/MMU_SUB_PAGES];
    CHECK(!vm_decommit(s, P(base + 8192), 4096));
    CHECK(c->sub[(base >> 12)/MMU_SUB_PAGES] == sub);
    s->sequence = UINT32_MAX - 1; inject_space = s;
    inject_tickets[0] = t[0]; inject_tickets[1] = t[1];
    /* Resolve after the old run was scanned, just before its validation mask. */
    irq_countdown = 2; irq_inject = interrupt_scan;
    vm_info_t info;
    CHECK(!vm_query(s, v, &info));
    CHECK(!irq_inject && s->sequence == 0 && info.state == VM_COMMITTED);
    CHECK(info.protection == VM_READ && info.run_base == v && info.run_size == 8192);
    CHECK(!vm_fault_release(t[0])); CHECK(c->sub[(base >> 12)/MMU_SUB_PAGES] == sub);
    CHECK(!vm_fault_release(t[1]));
    CHECK(!vm_release(s, v) && !vm_space_destroy(s)); drop(0); CHECK(!live);
  }
}
static void fault_callers(void) {
    vm_space_t *s = vm_space_create(context(0), P(BASE), 4096);
    void *v; vm_fault_t *t, *out = (void *)123;
    CHECK(s && !vm_reserve(s, NULL, 4096, &v));
    CHECK(!vm_fault_arm(s, v, VM_FAULT_ZERO_COMMIT, VM_READ, &t));
    vm_fault_info_t info, before; memset(&info, 0x51, sizeof(info)); before = info;
    const unsigned masks[] = {0x10, 0x20, 0x40, 0x80, 0xf0, 0x10000000, 0};
    for(unsigned i = 0; i < 7; ++i) {
        test_sr = masks[i]; test_irq = i == 6; unsigned n = attempts;
        CHECK(vm_fault_arm(s, v, VM_FAULT_ZERO_COMMIT, VM_READ, &out) < 0 && errno == EPERM);
        CHECK(vm_fault_query(t, &info) < 0 && errno == EPERM);
        CHECK(vm_fault_release(t) < 0 && errno == EPERM);
        CHECK(out == (void *)123 && !memcmp(&info, &before, sizeof(info)) && attempts == n);
    }
    test_sr = 0; test_irq = 0; fail_lock = 1;
    CHECK(vm_fault_query(t, &info) < 0 && errno == EAGAIN);
    CHECK(vm_fault_release(t) < 0 && errno == EAGAIN);
    fail_lock = 0;
    CHECK(!vm_fault_release(t)); CHECK(!vm_release(s, v) && !vm_space_destroy(s)); drop(0); CHECK(!live);
}


/* Caller-owned retirement: a queued query is still a handle borrower. Joining
   that borrower does not retire the independently suspended page callback. */
typedef struct { vm_fault_t *ticket; unsigned char *page; atomic_bool resume; } retirement_t;
static void *query_waiter(void *arg) {
    vm_fault_info_t info;
    CHECK(!vm_fault_query(arg, &info) && info.state == VM_FAULT_ARMED);
    return NULL;
}
static void *page_callback(void *arg) {
    retirement_t *r = arg;
    while(!atomic_load(&r->resume)) sched_yield();
    CHECK(r->page[73] == 0x42); r->page[74] = 0x43;
    return NULL;
}
static void *sole_releaser(void *arg) { CHECK(!vm_fault_release(arg)); return NULL; }
static void fault_retirement(void) {
    vm_space_t *s = vm_space_create(context(0), P(BASE), 4096);
    void *v; vm_fault_t *t;
    CHECK(s && !vm_reserve(s, NULL, 4096, &v) && !vm_commit(s, v, 4096, VM_READWRITE));
    unsigned char *backing = s->regions->pages[0].backing; backing[73] = 0x42;
    CHECK(!vm_fault_arm(s, v, VM_FAULT_GUARD, VM_READWRITE, &t));
    retirement_t r = {.ticket=t, .page=backing};
    pthread_t query, callback, releaser;
    CHECK(!mutex_lock(&s->mutex)); watched_mutex = &s->mutex; atomic_store(&mutex_waiters, 0);
    CHECK(!pthread_create(&query, NULL, query_waiter, t));
    while(!atomic_load(&mutex_waiters)) sched_yield();
    CHECK(!pthread_create(&callback, NULL, page_callback, &r));
    /* Close external admission here. No later thread is given a borrowed handle.
       Cancellation alone would not drain callback: explicitly resume and join. */
    CHECK(!mutex_unlock(&s->mutex)); CHECK(!pthread_join(query, NULL));
    CHECK(s->ticket_count == 1 && !atomic_load(&r.resume));
    atomic_store(&r.resume, true); CHECK(!pthread_join(callback, NULL));
    CHECK(backing[74] == 0x43);
    CHECK(!mutex_lock(&s->mutex)); atomic_store(&mutex_waiters, 0);
    CHECK(!pthread_create(&releaser, NULL, sole_releaser, t));
    while(!atomic_load(&mutex_waiters)) sched_yield();
    CHECK(s->ticket_count == 1); CHECK(!mutex_unlock(&s->mutex));
    CHECK(!pthread_join(releaser, NULL)); watched_mutex = NULL;
    expect(s, BASE, VM_COMMITTED, 4096, VM_READWRITE);
    CHECK(!vm_release(s, v) && !vm_space_destroy(s)); drop(0); CHECK(!live);
}
#include "pin-tests.inc"

int main(void) {
    bounds(); lifetime(); failures(); callers(); ranges_and_runs();
    rollback_preserves_neighbor(); separation(); preexisting_mapping(); serialization();
    fault_lifetime(); fault_failures(); fault_admission(); fault_query_and_pins(); fault_callers(); fault_retirement();
    pin_tests();
    printf("VM production PASS: %u checks; cache purges=%u TLB retirements=%u\n",
           atomic_load(&checks), purges, invalidations);
    return 0;
}
