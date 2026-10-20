/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <kos/vm.h>
#include <kos/fs.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <kos/cache.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <dc/memory.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "vm-internal.h"

#define VM_PAGE_BYTES 4096u
#define VM_ARENA_END 0x7c000000u

typedef struct vm_page {
    void *backing;
    unsigned protection;
    vm_fault_t *ticket;
    uint32_t pins;
} vm_page_t;

typedef struct vm_region {
    struct vm_region *next;
    uintptr_t base;
    size_t bytes;
    vm_file_t *file;
    vm_page_t pages[];
} vm_region_t;

typedef struct vm_fault_table {
    vm_fault_t *tickets[MMU_SUB_PAGES];
    unsigned count;
} vm_fault_table_t;

struct vm_space {
    mmucontext_t *context;
    uintptr_t base;
    size_t bytes;
    vm_region_t *regions;
    mutex_t mutex;
    vm_fault_table_t *faults[MMU_PAGES];
    uint64_t generation;
    unsigned ticket_count;
    uint32_t sequence;
    uint32_t pin_count;
};

struct vm_file {
    vm_space_t *space;
    vm_region_t *region;
    file_t fd;
    void *staging;
    vm_file_info_t info;
    size_t loading_page;
};

struct vm_pin {
    vm_space_t *space;
    vm_region_t *region;
    size_t first, count;
    vm_pin_info_t info;
    vm_phys_run_t runs[];
};

struct vm_fault {
    vm_space_t *space;
    vm_page_t *owner;
    void *backing;
    mmupage_t prepared;
    unsigned vpn;
    vm_fault_info_t info;
};

/* Borrowed unique-live-ASID ownership. Publish/detach under IRQ exclusion. */
static vm_space_t *vm_spaces[256];

static int vm_error(int error) {
    errno = error;
    return -1;
}

static int vm_caller(void) {
    uint32_t sr;
    __asm__ volatile("stc sr, %0" : "=r"(sr));
    if(irq_inside_int() || (sr & 0x100000f0u))
        return vm_error(EPERM);
    return 0;
}

static bool vm_size(size_t bytes) {
    return bytes && !(bytes % VM_PAGE_BYTES);
}

static bool vm_range(vm_space_t *s, uintptr_t base, size_t bytes) {
    return vm_size(bytes) && !(base % VM_PAGE_BYTES) && base >= s->base &&
           base - s->base < s->bytes && bytes <= s->bytes - (base - s->base);
}

/* Caller holds the space mutex. Validate before allocation or mutation. */
static vm_region_t *vm_region_range(vm_space_t *s, uintptr_t base, size_t bytes) {
    for(vm_region_t *r = s->regions; r && r->base <= base; r = r->next) {
        if(base - r->base < r->bytes) {
            if(bytes <= r->bytes - (base - r->base))
                return r;
            vm_error(EINVAL);
            return NULL;
        }
    }
    vm_error(ENOENT);
    return NULL;
}

static int vm_map_page(vm_space_t *s, uintptr_t address, vm_page_t *p,
                       vm_prot_t prot) {
    return mmu_page_map_ex(s->context, address / VM_PAGE_BYTES,
            ((uintptr_t)p->backing & MEM_AREA_CACHE_MASK) / VM_PAGE_BYTES, 1,
            prot == VM_READ ? MMU_KERNEL_RDONLY : MMU_KERNEL_RDWR,
            MMU_CACHE_BACK, false, true);
}

/* Validated ranges and exclusive context mutation make unmap infallible. */
static void vm_unmap(vm_space_t *s, uintptr_t address, size_t bytes) {
    int result = mmu_page_unmap_ex(s->context, address / VM_PAGE_BYTES,
                                  bytes / VM_PAGE_BYTES);
    assert(result == 0);
    (void)result;
}

static void vm_free_pages(vm_page_t *p, size_t count) {
    for(size_t i = 0; i < count; ++i) {
        free(p[i].backing);
        p[i] = (vm_page_t){0};
    }
}

vm_space_t *vm_space_create(mmucontext_t *context, void *arena, size_t bytes) {
    uintptr_t base = (uintptr_t)arena;
    if(vm_caller() < 0)
        return NULL;
    if(!context || context->asid < 0 || context->asid > 255 ||
       base < VM_PAGE_BYTES || base >= VM_ARENA_END || base % VM_PAGE_BYTES ||
       !vm_size(bytes) || bytes > VM_ARENA_END - base) {
        vm_error(EINVAL);
        return NULL;
    }
    for(size_t i = 0; i < bytes / VM_PAGE_BYTES; ++i) {
        if(mmu_virt_to_phys(context, base / VM_PAGE_BYTES + i) >= 0) {
            vm_error(EEXIST);
            return NULL;
        }
    }
    vm_space_t *s = calloc(1, sizeof(*s));
    if(!s) {
        vm_error(ENOMEM);
        return NULL;
    }
    s->context = context;
    s->base = base;
    s->bytes = bytes;
    if(mutex_init(&s->mutex, MUTEX_TYPE_NORMAL) < 0) {
        int error = errno;
        free(s);
        errno = error;
        return NULL;
    }
    bool occupied;
    {
        irq_disable_scoped();
        occupied = vm_spaces[context->asid] != NULL;
        if(!occupied)
            vm_spaces[context->asid] = s;
    }
    if(occupied) {
        mutex_destroy(&s->mutex);
        free(s);
        vm_error(EEXIST);
        return NULL;
    }
    return s;
}

int vm_space_destroy(vm_space_t *s) {
    if(vm_caller() < 0)
        return -1;
    if(!s)
        return vm_error(EINVAL);
    if(mutex_lock(&s->mutex) < 0)
        return -1;
    if(s->regions || s->ticket_count || s->pin_count) {
        mutex_unlock(&s->mutex);
        return vm_error(EBUSY);
    }
    mutex_unlock(&s->mutex);
    /* Caller has closed admission and joined even threads awaiting this lock. */
    {
        irq_disable_scoped();
        vm_spaces[s->context->asid] = NULL;
    }
    mutex_destroy(&s->mutex);
    free(s);
    return 0;
}

/* Space locked; publish reservation and optional file ownership together. */
static vm_region_t *vm_reserve_locked(vm_space_t *s, void *requested,
                                      size_t bytes, vm_file_t *file) {
    uintptr_t base = (uintptr_t)requested;
    vm_region_t **link = &s->regions;
    if(!requested) {
        base = s->base;
        while(*link && bytes > (*link)->base - base) {
            base = (*link)->base + (*link)->bytes;
            link = &(*link)->next;
        }
        if(bytes > s->bytes - (base - s->base)) {
            vm_error(ENOMEM);
            return NULL;
        }
    }
    else {
        while(*link && (*link)->base + (*link)->bytes <= base)
            link = &(*link)->next;
        if(*link && (*link)->base < base + bytes) {
            vm_error(EEXIST);
            return NULL;
        }
    }
    size_t count = bytes / VM_PAGE_BYTES;
    if(count > (SIZE_MAX - sizeof(vm_region_t)) / sizeof(vm_page_t)) {
        vm_error(file ? EOVERFLOW : ENOMEM);
        return NULL;
    }
    size_t metadata = sizeof(vm_region_t) + count * sizeof(vm_page_t);
    if(file) {
        /* Reserve the complete payload/table envelope before publication. */
        size_t span = VM_PAGE_BYTES * MMU_SUB_PAGES;
        size_t tables = (base + bytes - 1) / span - base / span + 1;
        size_t table_bytes = tables * sizeof(mmusubcontext_t) + sizeof(mmusubcontext_t *);
        if(metadata > SIZE_MAX - sizeof(*file) - VM_PAGE_BYTES) {
            vm_error(EOVERFLOW);
            return NULL;
        }
        size_t fixed = metadata + sizeof(*file) + VM_PAGE_BYTES;
        if(table_bytes > SIZE_MAX - fixed ||
           file->info.resident_limit > (SIZE_MAX - fixed - table_bytes) / VM_PAGE_BYTES) {
            vm_error(EOVERFLOW);
            return NULL;
        }
        if(fixed + table_bytes + file->info.resident_limit * VM_PAGE_BYTES >
           file->info.budget_bytes) {
            vm_error(EAGAIN);
            return NULL;
        }
        file->info.fixed_bytes = fixed;
        file->info.table_reserve_bytes = table_bytes;
        file->info.charged_bytes = fixed + table_bytes;
    }
    vm_region_t *r = calloc(1, sizeof(*r) + count * sizeof(vm_page_t));
    if(!r) {
        vm_error(ENOMEM);
        return NULL;
    }
    r->file = file;
    r->base = base;
    r->bytes = bytes;
    r->next = *link;
    *link = r;
    return r;
}

int vm_reserve(vm_space_t *s, void *requested, size_t bytes, void **result) {
    if(vm_caller() < 0)
        return -1;
    if(!s || !result || !vm_size(bytes) ||
       (requested && !vm_range(s, (uintptr_t)requested, bytes)))
        return vm_error(EINVAL);
    if(mutex_lock(&s->mutex) < 0)
        return -1;
    vm_region_t *r = vm_reserve_locked(s, requested, bytes, NULL);
    if(r)
        *result = (void *)r->base;
    mutex_unlock(&s->mutex);
    return r ? 0 : -1;
}

/* Common admission; returns with the space locked on success. */
static vm_region_t *vm_begin(vm_space_t *s, void *address, size_t bytes,
                              unsigned prot, bool check_prot) {
    if(vm_caller() < 0)
        return NULL;
    if(!s || !vm_range(s, (uintptr_t)address, bytes)) {
        vm_error(EINVAL);
        return NULL;
    }
    if(check_prot && prot != VM_READ && prot != VM_READWRITE) {
        vm_error(ENOTSUP);
        return NULL;
    }
    if(mutex_lock(&s->mutex) < 0)
        return NULL;
    vm_region_t *r = vm_region_range(s, (uintptr_t)address, bytes);
    if(r && r->file) {
        vm_error(EBUSY);
        r = NULL;
    }
    if(r) {
        size_t first = ((uintptr_t)address - r->base) / VM_PAGE_BYTES;
        for(size_t i = 0; i < bytes / VM_PAGE_BYTES; ++i) {
            if(r->pages[first + i].ticket || r->pages[first + i].pins) {
                vm_error(EBUSY);
                r = NULL;
                break;
            }
        }
    }
    if(!r)
        mutex_unlock(&s->mutex);
    return r;
}

int vm_commit(vm_space_t *s, void *address, size_t bytes, vm_prot_t prot) {
    vm_region_t *r = vm_begin(s, address, bytes, prot, true);
    if(!r)
        return -1;
    size_t first = ((uintptr_t)address - r->base) / VM_PAGE_BYTES;
    size_t count = bytes / VM_PAGE_BYTES, mapped = 0;
    vm_page_t *pages = r->pages + first;
    for(size_t i = 0; i < count; ++i) {
        if(pages[i].protection) {
            mutex_unlock(&s->mutex);
            return vm_error(EALREADY);
        }
    }
    for(size_t i = 0; i < count; ++i) {
        pages[i].backing = aligned_alloc(VM_PAGE_BYTES, VM_PAGE_BYTES);
        if(!pages[i].backing) {
            errno = ENOMEM;
            goto rollback;
        }
        memset(pages[i].backing, 0, VM_PAGE_BYTES);
        dcache_purge_range((uintptr_t)pages[i].backing, VM_PAGE_BYTES);
    }
    for(; mapped < count; ++mapped) {
        if(vm_map_page(s, (uintptr_t)address + mapped * VM_PAGE_BYTES,
                       &pages[mapped], prot) < 0)
            goto rollback;
    }
    for(size_t i = 0; i < count; ++i)
        pages[i].protection = prot;
    mutex_unlock(&s->mutex);
    return 0;

rollback: {
        int error = errno;
        if(mapped)
            vm_unmap(s, (uintptr_t)address, mapped * VM_PAGE_BYTES);
        vm_free_pages(pages, count);
        mutex_unlock(&s->mutex);
        return vm_error(error);
    }
}

int vm_protect(vm_space_t *s, void *address, size_t bytes, vm_prot_t prot) {
    vm_region_t *r = vm_begin(s, address, bytes, prot, true);
    if(!r)
        return -1;
    size_t first = ((uintptr_t)address - r->base) / VM_PAGE_BYTES;
    size_t count = bytes / VM_PAGE_BYTES;
    vm_page_t *pages = r->pages + first;
    for(size_t i = 0; i < count; ++i) {
        if(!pages[i].protection) {
            mutex_unlock(&s->mutex);
            return vm_error(ENOENT);
        }
    }
    for(size_t i = 0; i < count; ++i) {
        if(pages[i].protection != (unsigned)prot) {
            /* Existing mappings retain their tables: no allocation/failure. */
            int result = vm_map_page(s, (uintptr_t)address + i * VM_PAGE_BYTES,
                                     &pages[i], prot);
            assert(result == 0);
            (void)result;
            pages[i].protection = prot;
        }
    }
    mutex_unlock(&s->mutex);
    return 0;
}

int vm_decommit(vm_space_t *s, void *address, size_t bytes) {
    vm_region_t *r = vm_begin(s, address, bytes, 0, false);
    if(!r)
        return -1;
    vm_unmap(s, (uintptr_t)address, bytes);
    vm_free_pages(r->pages + ((uintptr_t)address - r->base) / VM_PAGE_BYTES,
                   bytes / VM_PAGE_BYTES);
    mutex_unlock(&s->mutex);
    return 0;
}

int vm_release(vm_space_t *s, void *base) {
    if(vm_caller() < 0)
        return -1;
    if(!s || !vm_range(s, (uintptr_t)base, VM_PAGE_BYTES))
        return vm_error(EINVAL);
    if(mutex_lock(&s->mutex) < 0)
        return -1;
    vm_region_t **link = &s->regions;
    while(*link && (*link)->base < (uintptr_t)base)
        link = &(*link)->next;
    vm_region_t *r = *link;
    if(!r || r->base != (uintptr_t)base) {
        mutex_unlock(&s->mutex);
        return vm_error(ENOENT);
    }
    if(r->file) {
        mutex_unlock(&s->mutex);
        return vm_error(EBUSY);
    }
    for(size_t i = 0; i < r->bytes / VM_PAGE_BYTES; ++i) {
        if(r->pages[i].ticket || r->pages[i].pins) {
            mutex_unlock(&s->mutex);
            return vm_error(EBUSY);
        }
    }
    vm_unmap(s, r->base, r->bytes);
    vm_free_pages(r->pages, r->bytes / VM_PAGE_BYTES);
    *link = r->next;
    free(r);
    mutex_unlock(&s->mutex);
    return 0;
}

static vm_state_t vm_page_state(const vm_page_t *p) {
    if(p->ticket && p->ticket->info.kind == VM_FAULT_GUARD &&
       p->ticket->info.state == VM_FAULT_ARMED)
        return VM_GUARDED;
    return p->protection ? VM_COMMITTED : VM_RESERVED;
}

int vm_query(vm_space_t *s, const void *address, vm_info_t *result) {
    uintptr_t addr = (uintptr_t)address;
    if(vm_caller() < 0)
        return -1;
    if(!s || !result || addr < s->base || addr - s->base >= s->bytes)
        return vm_error(EINVAL);
    if(mutex_lock(&s->mutex) < 0)
        return -1;
retry: ;
    uint32_t sequence;
    {
        irq_disable_scoped();
        sequence = s->sequence;
    }
    uintptr_t begin = s->base;
    vm_region_t *r = s->regions;
    while(r && r->base + r->bytes <= addr) {
        begin = r->base + r->bytes;
        r = r->next;
    }
    vm_info_t info = {0};
    if(!r || addr < r->base) {
        info.state = VM_FREE;
        info.run_base = (void *)begin;
        info.run_size = (r ? r->base : s->base + s->bytes) - begin;
    }
    else {
        size_t first = (addr - r->base) / VM_PAGE_BYTES;
        size_t end = first + 1, count = r->bytes / VM_PAGE_BYTES;
        unsigned prot = r->pages[first].protection;
        vm_state_t state = vm_page_state(&r->pages[first]);
        while(first && r->pages[first - 1].protection == prot &&
              vm_page_state(&r->pages[first - 1]) == state)
            --first;
        while(end < count && r->pages[end].protection == prot &&
              vm_page_state(&r->pages[end]) == state)
            ++end;
        info.region_base = (void *)r->base;
        info.region_size = r->bytes;
        info.run_base = (void *)(r->base + first * VM_PAGE_BYTES);
        info.run_size = (end - first) * VM_PAGE_BYTES;
        info.state = state;
        info.protection = prot;
    }
    {
        irq_disable_scoped();
        /* Only already-armed pages can resolve while this mutex is held.
           There are fewer than 2^19 pages: sequence cannot wrap a whole 2^32
           during this scan. Avoid a page-count-sized IRQ exclusion. */
        if(sequence != s->sequence)
            goto retry;
        *result = info;
    }
    mutex_unlock(&s->mutex);
    return 0;
}

int vm_fault_arm(vm_space_t *s, void *address, vm_fault_kind_t kind,
                 vm_prot_t prot, vm_fault_t **result) {
    if(vm_caller() < 0)
        return -1;
    if(!result || (kind != VM_FAULT_ZERO_COMMIT && kind != VM_FAULT_GUARD))
        return vm_error(EINVAL);
    vm_region_t *r = vm_begin(s, address, VM_PAGE_BYTES, prot, true);
    if(!r)
        return -1;
    vm_page_t *p = &r->pages[((uintptr_t)address - r->base) / VM_PAGE_BYTES];
    int error;
    if((kind == VM_FAULT_ZERO_COMMIT) != (p->protection == 0)) {
        error = EALREADY;
        goto fail_state;
    }
    if(kind == VM_FAULT_GUARD && p->protection != (unsigned)prot) {
        error = EINVAL;
        goto fail_state;
    }
    if(s->generation == UINT64_MAX) {
        error = EOVERFLOW;
        goto fail_state;
    }
    unsigned vpn = (uintptr_t)address / VM_PAGE_BYTES;
    unsigned top = vpn / MMU_SUB_PAGES;
    vm_fault_table_t *table = s->faults[top];
    vm_fault_t *t = calloc(1, sizeof(*t));
    if(!t) {
        error = ENOMEM;
        goto fail_state;
    }
    t->backing = p->backing;
    if(kind == VM_FAULT_ZERO_COMMIT) {
        t->backing = aligned_alloc(VM_PAGE_BYTES, VM_PAGE_BYTES);
        if(!t->backing) goto fail_ticket;
        memset(t->backing, 0, VM_PAGE_BYTES);
        dcache_purge_range((uintptr_t)t->backing, VM_PAGE_BYTES);
    }
    if(!table) {
        table = calloc(1, sizeof(*table));
        if(!table) goto fail_ticket;
    }
    if(mmu_vm_prepare(s->context, vpn,
            ((uintptr_t)t->backing & MEM_AREA_CACHE_MASK) / VM_PAGE_BYTES,
            prot == VM_READ ? MMU_KERNEL_RDONLY : MMU_KERNEL_RDWR,
            &t->prepared) < 0)
        goto fail_ticket;

    /* Preparation succeeded; the remaining transition cannot allocate/fail.
       All affected raw-pointer users are quiescent before admission. */
    t->space = s;
    t->owner = p;
    t->vpn = vpn;
    t->info.page = address;
    t->info.kind = kind;
    t->info.protection = prot;
    t->info.generation = ++s->generation;
    t->info.state = VM_FAULT_ARMED;
    mmu_vm_hide(s->context, vpn);
    {
        irq_disable_scoped();
        s->faults[top] = table;
        table->tickets[vpn % MMU_SUB_PAGES] = t;
        ++table->count;
        ++s->ticket_count;
        p->ticket = t;
    }
    *result = t;
    mutex_unlock(&s->mutex);
    return 0;

fail_ticket:
    if(table != s->faults[top]) free(table);
    if(kind == VM_FAULT_ZERO_COMMIT) free(t->backing);
    free(t);
    error = ENOMEM;
fail_state:
    mutex_unlock(&s->mutex);
    return vm_error(error);
}

int vm_fault_query(vm_fault_t *t, vm_fault_info_t *result) {
    if(vm_caller() < 0)
        return -1;
    if(!t || !result)
        return vm_error(EINVAL);
    vm_space_t *s = t->space;
    if(mutex_lock(&s->mutex) < 0)
        return -1;
    {
        irq_disable_scoped();
        *result = t->info;
    }
    mutex_unlock(&s->mutex);
    return 0;
}

int vm_fault_release(vm_fault_t *t) {
    if(vm_caller() < 0)
        return -1;
    if(!t)
        return vm_error(EINVAL);
    /* R2: caller has closed ticket admission and joined all other handle/API
       users, including mutex waiters, separately from affected page users. */
    vm_space_t *s = t->space;
    if(mutex_lock(&s->mutex) < 0)
        return -1;
    unsigned top = t->vpn / MMU_SUB_PAGES;
    vm_fault_table_t *table = s->faults[top];
    bool unused = t->info.state == VM_FAULT_ARMED;
    {
        irq_disable_scoped();
        table->tickets[t->vpn % MMU_SUB_PAGES] = NULL;
        t->owner->ticket = NULL;
        --table->count;
        --s->ticket_count;
        if(!table->count) s->faults[top] = NULL;
        if(unused && t->info.kind == VM_FAULT_GUARD)
            s->context->sub[top]->page[t->vpn % MMU_SUB_PAGES] = t->prepared;
    }
    if(unused && t->info.kind == VM_FAULT_ZERO_COMMIT)
        mmu_vm_hide(s->context, t->vpn);
    mmu_vm_unpin(s->context, t->vpn);
    if(unused && t->info.kind == VM_FAULT_ZERO_COMMIT) free(t->backing);
    if(!table->count) free(table);
    free(t);
    mutex_unlock(&s->mutex);
    return 0;
}

static bool vm_direct(uint32_t address) {
    return address >= 0x80000000u && address < 0xa0000000u;
}

mmupage_t *vm_fault_resolve(mmucontext_t *c, irq_t event,
                          const irq_context_t *frame, uint32_t address,
                          uint32_t fault_pteh, uint32_t control,
                          uintptr_t carrier) {
    /* Raw sentinel check must precede any logical stack conversion. No stack
       discovery/provider/TLS callback: valid direct extent is caller-owned. */
    if((event != EXC_DTLB_MISS_READ && event != EXC_DTLB_MISS_WRITE) ||
       !c || c != mmu_cxt_current || c->asid < 0 || c->asid > 255 || !(control & 1) ||
       (fault_pteh & 255) != (unsigned)c->asid ||
       (frame->sr & 0x700000f0u) != 0x40000000u ||
       frame->r[15] >= UINT32_MAX - 127u ||
       !vm_direct(frame->pc) || !vm_direct(frame->r[15]) ||
       (frame->pc & 1) || (frame->r[15] & 3))
        return NULL;
    vm_space_t *s = vm_spaces[c->asid];
    if(!s || s->context != c || address < s->base || address - s->base >= s->bytes)
        return NULL;
    unsigned vpn = address / VM_PAGE_BYTES;
    vm_fault_table_t *table = s->faults[vpn / MMU_SUB_PAGES];
    vm_fault_t *t = table ? table->tickets[vpn % MMU_SUB_PAGES] : NULL;
    if(!t || t->info.state != VM_FAULT_ARMED ||
       (event == EXC_DTLB_MISS_WRITE && t->info.protection != VM_READWRITE))
        return NULL;
    mmusubcontext_t *sub = c->sub[vpn / MMU_SUB_PAGES];
    mmupage_t *pte = sub ? &sub->page[vpn % MMU_SUB_PAGES] : NULL;
    if(!pte || pte->valid || !(pte->blank & MMU_VM_PIN) ||
       !t->prepared.valid || !(t->prepared.ptel & 0x100))
        return NULL; /* No state progress / prepared publication possible. */

    vm_fault_snapshot_t *snap = &t->info.snapshot;
    snap->event = event;
    snap->address = address;
    snap->pteh = fault_pteh;
    snap->mmucr = control;
    snap->pc = frame->pc;
    snap->sr = frame->sr;
    snap->sp = frame->r[15];
    snap->table = (uintptr_t)c;
    snap->carrier = carrier;
    snap->asid = c->asid;
    snap->access = event == EXC_DTLB_MISS_WRITE ? VM_FAULT_WRITE : VM_FAULT_READ_OR_FETCH;
    snap->classification = VM_FAULT_MANAGED;
    snap->outcome = VM_FAULT_RETRY;
    *pte = t->prepared;
    t->owner->backing = t->backing;
    t->owner->protection = t->info.protection;
    t->info.snapshot_valid = true;
    t->info.state = VM_FAULT_RESOLVED;
    ++s->sequence;
    return pte;
}

/* Byte-range admission is deliberately separate from page-aligned mutations.
   The category scans give tickets precedence over holes/protection errors. */
int vm_pin_acquire(vm_space_t *s, const void *address, size_t bytes,
                   vm_pin_access_t access, vm_pin_t **result) {
    uintptr_t base = (uintptr_t)address;
    if(vm_caller() < 0)
        return -1;
    if(!s || !result || !base || !bytes)
        return vm_error(EINVAL);
    if(bytes > UINTPTR_MAX - base)
        return vm_error(EOVERFLOW);
    if(base < s->base || base - s->base >= s->bytes ||
       bytes > s->bytes - (base - s->base))
        return vm_error(EINVAL);
    if(access != VM_PIN_READ && access != VM_PIN_WRITE &&
       access != VM_PIN_READWRITE)
        return vm_error(ENOTSUP);
    if(mutex_lock(&s->mutex) < 0)
        return -1;
    vm_region_t *r = vm_region_range(s, base, bytes);
    if(!r) {
        mutex_unlock(&s->mutex);
        return -1;
    }
    size_t first = (base - r->base) / VM_PAGE_BYTES;
    size_t last = (base - r->base + bytes - 1) / VM_PAGE_BYTES;
    size_t count = last - first + 1;
    vm_page_t *pages = r->pages + first;
    int error = 0;
    /* Resolution leaves the ticket pointer installed. Once this scan passes,
       no exception can change these pages; arm/release share this mutex. */
    for(size_t i = 0; i < count; ++i) {
        if(pages[i].ticket) { error = EBUSY; goto fail; }
    }
    for(size_t i = 0; i < count; ++i) {
        if(!pages[i].backing || !pages[i].protection) {
            error = ENOENT;
            goto fail;
        }
    }
    if(access & VM_PIN_WRITE) {
        for(size_t i = 0; i < count; ++i) {
            if(pages[i].protection != VM_READWRITE) {
                error = EACCES;
                goto fail;
            }
        }
    }
    if(count > (SIZE_MAX - sizeof(vm_pin_t)) / sizeof(vm_phys_run_t) ||
       s->pin_count == UINT32_MAX) {
        error = EOVERFLOW;
        goto fail;
    }
    for(size_t i = 0; i < count; ++i) {
        if(pages[i].pins == UINT32_MAX) { error = EOVERFLOW; goto fail; }
    }
    vm_pin_t *p = malloc(sizeof(*p) + count * sizeof(*p->runs));
    if(!p) { error = ENOMEM; goto fail; }
    p->space = s;
    p->region = r;
    p->first = first;
    p->count = count;
    p->info = (vm_pin_info_t){.address = address, .bytes = bytes,
        .access = access, .context = s->context, .asid = s->context->asid};
    size_t offset = 0;
    for(size_t i = 0; i < count; ++i) {
        size_t edge = i ? 0 : base % VM_PAGE_BYTES;
        size_t length = VM_PAGE_BYTES - edge;
        if(length > bytes - offset) length = bytes - offset;
        uintptr_t physical = ((uintptr_t)pages[i].backing & MEM_AREA_CACHE_MASK) + edge;
        vm_phys_run_t *prev = p->info.run_count ? &p->runs[p->info.run_count - 1] : NULL;
        if(prev && prev->bytes <= UINTPTR_MAX - prev->physical &&
           prev->physical + prev->bytes == physical &&
           prev->offset + prev->bytes == offset) {
            prev->bytes += length;
        }
        else {
            p->runs[p->info.run_count++] = (vm_phys_run_t){physical, offset, length};
        }
        offset += length;
    }
    /* Nothing after this point can fail. Publish the complete independent lease. */
    for(size_t i = 0; i < count; ++i) ++pages[i].pins;
    ++s->pin_count;
    *result = p;
    mutex_unlock(&s->mutex);
    return 0;
fail:
    mutex_unlock(&s->mutex);
    return vm_error(error);
}

int vm_pin_query(vm_pin_t *p, vm_pin_info_t *result) {
    if(vm_caller() < 0)
        return -1;
    if(!p || !result)
        return vm_error(EINVAL);
    if(mutex_lock(&p->space->mutex) < 0)
        return -1;
    *result = p->info;
    mutex_unlock(&p->space->mutex);
    return 0;
}

int vm_pin_runs(vm_pin_t *p, size_t first, vm_phys_run_t *runs,
                size_t capacity, size_t *written) {
    if(vm_caller() < 0)
        return -1;
    if(!p || !written || (capacity && !runs))
        return vm_error(EINVAL);
    if(capacity > SIZE_MAX / sizeof(*runs))
        return vm_error(EOVERFLOW);
    if(mutex_lock(&p->space->mutex) < 0)
        return -1;
    if(first > p->info.run_count) {
        mutex_unlock(&p->space->mutex);
        return vm_error(EINVAL);
    }
    size_t count = p->info.run_count - first;
    if(count > capacity) count = capacity;
    if(count) memcpy(runs, p->runs + first, count * sizeof(*runs));
    *written = count;
    mutex_unlock(&p->space->mutex);
    return 0;
}

int vm_pin_release(vm_pin_t *p) {
    if(vm_caller() < 0)
        return -1;
    if(!p)
        return vm_error(EINVAL);
    /* Owner has closed admission and joined handle/API waiters AND consumers.
       The mutex cannot establish that protocol or detect stale handles. */
    vm_space_t *s = p->space;
    if(mutex_lock(&s->mutex) < 0)
        return -1;
    for(size_t i = 0; i < p->count; ++i)
        --p->region->pages[p->first + i].pins;
    --s->pin_count;
    mutex_unlock(&s->mutex);
    free(p);
    return 0;
}

/* File sources are private, immutable and synchronous. No manager lock spans
   VFS calls; an active-load claim retains ownership while this lock is dropped. */
int vm_file_open(vm_space_t *s, const char *path,
                 const vm_file_options_t *options, vm_file_t **result) {
    if(vm_caller() < 0)
        return -1;
    if(!s || !path || !options || !result || !options->bytes ||
       !options->resident_limit)
        return vm_error(EINVAL);
    vm_file_options_t o = *options;
    if(o.bytes > SIZE_MAX - (VM_PAGE_BYTES - 1) ||
       o.offset > UINT64_MAX - o.bytes || o.offset + o.bytes > INT32_MAX)
        return vm_error(EOVERFLOW);
    size_t rounded = (o.bytes + VM_PAGE_BYTES - 1) & ~(size_t)(VM_PAGE_BYTES - 1);
    size_t pages = rounded / VM_PAGE_BYTES;
    if(pages > (SIZE_MAX - sizeof(vm_region_t)) / sizeof(vm_page_t))
        return vm_error(EOVERFLOW);
    if((o.requested && !vm_range(s, (uintptr_t)o.requested, rounded)) ||
       o.resident_limit > pages)
        return vm_error(EINVAL);
    vm_file_t *f = calloc(1, sizeof(*f));
    if(!f)
        return vm_error(ENOMEM);
    f->space = s;
    f->fd = fs_open(path, O_RDONLY);
    if(f->fd == FILEHND_INVALID) {
        int error = errno;
        free(f);
        return vm_error(error);
    }
    struct stat st;
    if(fs_fstat(f->fd, &st) < 0)
        goto fail;
    if(!S_ISREG(st.st_mode)) {
        errno = ENOTSUP;
        goto fail;
    }
    if(st.st_size < 0) {
        errno = EINVAL;
        goto fail;
    }
    if((uint64_t)st.st_size > INT32_MAX) {
        errno = EOVERFLOW;
        goto fail;
    }
    int64_t total = fs_total64(f->fd);
    if(total < 0)
        goto fail;
    if(total != st.st_size) {
        errno = EIO;
        goto fail;
    }
    if(o.offset + o.bytes > (uint64_t)total) {
        errno = EINVAL;
        goto fail;
    }
    f->info = (vm_file_info_t){.offset = o.offset, .bytes = o.bytes,
        .pages = pages, .resident_limit = o.resident_limit,
        .budget_bytes = o.budget_bytes};
    f->staging = aligned_alloc(VM_PAGE_BYTES, VM_PAGE_BYTES);
    if(!f->staging) {
        errno = ENOMEM;
        goto fail;
    }
    if(mutex_lock(&s->mutex) < 0)
        goto fail;
    f->region = vm_reserve_locked(s, o.requested, rounded, f);
    if(f->region) {
        f->info.address = (const void *)f->region->base;
        *result = f;
    }
    mutex_unlock(&s->mutex);
    if(f->region)
        return 0;
fail: {
        int error = errno;
        /* Eligible private fd + normal untimed lock: close reaches detach.
           Backend failure consumes too. Never retry a possibly reused number. */
        fs_close(f->fd);
        free(f->staging);
        free(f);
        return vm_error(error);
    }
}

int vm_file_query(vm_file_t *f, vm_file_info_t *result) {
    if(vm_caller() < 0)
        return -1;
    if(!f || !result)
        return vm_error(EINVAL);
    if(mutex_lock(&f->space->mutex) < 0)
        return -1;
    *result = f->info;
    mutex_unlock(&f->space->mutex);
    return 0;
}

static size_t vm_file_valid_bytes(vm_file_t *f, size_t page) {
    size_t bytes = f->info.bytes - page * VM_PAGE_BYTES;
    return bytes < VM_PAGE_BYTES ? bytes : VM_PAGE_BYTES;
}

int vm_file_page_query(vm_file_t *f, size_t page, vm_file_page_info_t *result) {
    if(vm_caller() < 0)
        return -1;
    if(!f || !result || page >= f->info.pages)
        return vm_error(EINVAL);
    if(mutex_lock(&f->space->mutex) < 0)
        return -1;
    vm_file_page_state_t state = VM_FILE_ABSENT;
    if(f->info.loading_pages && f->loading_page == page)
        state = VM_FILE_LOADING;
    else if(f->region->pages[page].protection)
        state = VM_FILE_READY;
    *result = (vm_file_page_info_t){state, vm_file_valid_bytes(f, page)};
    mutex_unlock(&f->space->mutex);
    return 0;
}

int vm_file_prefetch(vm_file_t *f, size_t page) {
    if(vm_caller() < 0)
        return -1;
    if(!f || page >= f->info.pages)
        return vm_error(EINVAL);
    vm_space_t *s = f->space;
    if(mutex_lock(&s->mutex) < 0)
        return -1;
    int error = 0;
    if(f->info.cancelled)
        error = ECANCELED;
    else if(f->info.loading_pages)
        error = EBUSY;
    else if(f->region->pages[page].protection) {
        mutex_unlock(&s->mutex);
        return 0;
    }
    else if(f->info.resident_pages == f->info.resident_limit)
        error = EAGAIN;
    if(error) {
        mutex_unlock(&s->mutex);
        return vm_error(error);
    }
    f->loading_page = page;
    f->info.loading_pages = 1;
    f->info.charged_bytes += VM_PAGE_BYTES;
    mutex_unlock(&s->mutex);

    vm_page_t candidate = {0};
    candidate.backing = aligned_alloc(VM_PAGE_BYTES, VM_PAGE_BYTES);
    if(!candidate.backing)
        error = ENOMEM;
    else {
        memset(candidate.backing, 0, VM_PAGE_BYTES);
        off_t offset = (off_t)(f->info.offset + page * VM_PAGE_BYTES);
        off_t position = fs_seek(f->fd, offset, SEEK_SET);
        if(position < 0)
            error = errno;
        else if(position != offset)
            error = EIO;
        else {
            size_t bytes = vm_file_valid_bytes(f, page);
            ssize_t read_bytes = fs_read(f->fd, f->staging, bytes);
            if(read_bytes < 0)
                error = errno;
            else if((size_t)read_bytes != bytes)
                error = EIO;
            else {
                memcpy(candidate.backing, f->staging, bytes);
                dcache_purge_range((uintptr_t)candidate.backing, VM_PAGE_BYTES);
            }
        }
    }
    /* The normal untimed mutex cannot fail under the live-owner/no-foreign-
       error-wake contract. The active claim survives all unlocked cleanup. */
    int locked = mutex_lock(&s->mutex);
    assert(locked == 0);
    (void)locked;
    if(f->info.cancelled)
        error = ECANCELED;
    if(!error && vm_map_page(s, f->region->base + page * VM_PAGE_BYTES,
                            &candidate, VM_READ) < 0)
        error = errno;
    if(!error) {
        candidate.protection = VM_READ;
        f->region->pages[page] = candidate;
        ++f->info.resident_pages;
    }
    else {
        free(candidate.backing);
        f->info.charged_bytes -= VM_PAGE_BYTES;
    }
    /* No operation can still access unpublished backing once LOADING clears. */
    f->info.loading_pages = 0;
    mutex_unlock(&s->mutex);
    return error ? vm_error(error) : 0;
}

int vm_file_evict(vm_file_t *f, size_t page) {
    if(vm_caller() < 0)
        return -1;
    if(!f || page >= f->info.pages)
        return vm_error(EINVAL);
    vm_space_t *s = f->space;
    if(mutex_lock(&s->mutex) < 0)
        return -1;
    vm_page_t *p = &f->region->pages[page];
    if((f->info.loading_pages && f->loading_page == page) || p->pins) {
        mutex_unlock(&s->mutex);
        return vm_error(EBUSY);
    }
    if(p->protection) {
        vm_unmap(s, f->region->base + page * VM_PAGE_BYTES, VM_PAGE_BYTES);
        vm_free_pages(p, 1);
        --f->info.resident_pages;
        f->info.charged_bytes -= VM_PAGE_BYTES;
    }
    mutex_unlock(&s->mutex);
    return 0;
}

int vm_file_cancel(vm_file_t *f) {
    if(vm_caller() < 0)
        return -1;
    if(!f)
        return vm_error(EINVAL);
    if(mutex_lock(&f->space->mutex) < 0)
        return -1;
    f->info.cancelled = 1;
    mutex_unlock(&f->space->mutex);
    return 0;
}

int vm_file_close(vm_file_t *f, int *source_close_error) {
    if(vm_caller() < 0)
        return -1;
    if(!f || !source_close_error)
        return vm_error(EINVAL);
    vm_space_t *s = f->space;
    if(mutex_lock(&s->mutex) < 0)
        return -1;
    if(f->info.loading_pages) {
        mutex_unlock(&s->mutex);
        return vm_error(EBUSY);
    }
    vm_region_t *r = f->region;
    for(size_t i = 0; i < f->info.pages; ++i) {
        if(r->pages[i].pins) {
            mutex_unlock(&s->mutex);
            return vm_error(EBUSY);
        }
    }
    vm_unmap(s, r->base, r->bytes);
    vm_free_pages(r->pages, f->info.pages);
    vm_region_t **link = &s->regions;
    while(*link != r)
        link = &(*link)->next;
    *link = r->next;
    free(r);
    mutex_unlock(&s->mutex);
    int error = fs_close(f->fd) < 0 ? errno : 0;
    free(f->staging);
    free(f);
    *source_close_error = error;
    return 0;
}
