/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <kos/vm.h>
#include <kos/cache.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <dc/memory.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VM_PAGE_BYTES 4096u
#define VM_ARENA_END 0x7c000000u

typedef struct vm_page {
    void *backing;
    unsigned protection;
} vm_page_t;

typedef struct vm_region {
    struct vm_region *next;
    uintptr_t base;
    size_t bytes;
    vm_page_t pages[];
} vm_region_t;

struct vm_space {
    mmucontext_t *context;
    uintptr_t base;
    size_t bytes;
    vm_region_t *regions;
    mutex_t mutex;
};

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
    return s;
}

int vm_space_destroy(vm_space_t *s) {
    if(vm_caller() < 0)
        return -1;
    if(!s)
        return vm_error(EINVAL);
    if(mutex_lock(&s->mutex) < 0)
        return -1;
    if(s->regions) {
        mutex_unlock(&s->mutex);
        return vm_error(EBUSY);
    }
    mutex_unlock(&s->mutex);
    /* Caller has closed admission and joined even threads awaiting this lock. */
    mutex_destroy(&s->mutex);
    free(s);
    return 0;
}

int vm_reserve(vm_space_t *s, void *requested, size_t bytes, void **result) {
    uintptr_t base = (uintptr_t)requested;
    if(vm_caller() < 0)
        return -1;
    if(!s || !result || !vm_size(bytes) ||
       (requested && !vm_range(s, base, bytes)))
        return vm_error(EINVAL);
    if(mutex_lock(&s->mutex) < 0)
        return -1;
    vm_region_t **link = &s->regions;
    if(!requested) {
        base = s->base;
        while(*link && bytes > (*link)->base - base) {
            base = (*link)->base + (*link)->bytes;
            link = &(*link)->next;
        }
        if(bytes > s->bytes - (base - s->base)) {
            mutex_unlock(&s->mutex);
            return vm_error(ENOMEM);
        }
    }
    else {
        while(*link && (*link)->base + (*link)->bytes <= base)
            link = &(*link)->next;
        if(*link && (*link)->base < base + bytes) {
            mutex_unlock(&s->mutex);
            return vm_error(EEXIST);
        }
    }
    size_t count = bytes / VM_PAGE_BYTES;
    if(count > (SIZE_MAX - sizeof(vm_region_t)) / sizeof(vm_page_t)) {
        mutex_unlock(&s->mutex);
        return vm_error(ENOMEM);
    }
    vm_region_t *r = calloc(1, sizeof(*r) + count * sizeof(vm_page_t));
    if(!r) {
        mutex_unlock(&s->mutex);
        return vm_error(ENOMEM);
    }
    r->base = base;
    r->bytes = bytes;
    r->next = *link;
    *link = r;
    *result = (void *)base;
    mutex_unlock(&s->mutex);
    return 0;
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
    vm_unmap(s, r->base, r->bytes);
    vm_free_pages(r->pages, r->bytes / VM_PAGE_BYTES);
    *link = r->next;
    free(r);
    mutex_unlock(&s->mutex);
    return 0;
}

int vm_query(vm_space_t *s, const void *address, vm_info_t *result) {
    uintptr_t addr = (uintptr_t)address;
    if(vm_caller() < 0)
        return -1;
    if(!s || !result || addr < s->base || addr - s->base >= s->bytes)
        return vm_error(EINVAL);
    if(mutex_lock(&s->mutex) < 0)
        return -1;
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
        while(first && r->pages[first - 1].protection == prot)
            --first;
        while(end < count && r->pages[end].protection == prot)
            ++end;
        info.region_base = (void *)r->base;
        info.region_size = r->bytes;
        info.run_base = (void *)(r->base + first * VM_PAGE_BYTES);
        info.run_size = (end - first) * VM_PAGE_BYTES;
        info.state = prot ? VM_COMMITTED : VM_RESERVED;
        info.protection = prot;
    }
    *result = info;
    mutex_unlock(&s->mutex);
    return 0;
}
