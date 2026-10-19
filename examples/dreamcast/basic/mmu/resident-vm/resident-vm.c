/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <kos.h>
#include <kos/vm.h>
#include <kos/fiber.h>
#include <dc/memory.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ADDRESS 0x101ff000u
#define BYTES 4096u
#define COOKIE 0x564d3137u
static volatile uint32_t *const data = (void *)ADDRESS;
static alignas(32) unsigned char stacks[2][8192];
static kfiber_t *children[2], *main_fiber;
static kthread_t *carrier;
static mmucontext_t *context;
static volatile bool stop;
static volatile int active = -1;
static volatile unsigned samples[2];
static unsigned failures, helper_failures, last_value;
static _Thread_local unsigned tls_cookie;
static int *carrier_errno;
static void failed(unsigned line, const char *expression) {
    int saved = errno;
    ++failures;
    printf("VM1 CHECK line=%u errno=%d: %s\n", line, saved, expression);
    errno = saved;
}
#define VERIFY(x) do { if(!(x)) failed(__LINE__, #x); } while(0)
#define REQUIRE(x) do { if(!(x)) { failed(__LINE__, #x); goto cleanup; } } while(0)

/* Seed only the separately owned preflight page in replaceable UTLB slot 0.
   Some runtimes enable general translation only after a non-SQ LDTLB. A
   software page-table entry alone then cannot provoke the first TLB miss.
   The normal checked unmap below retires this seed before backing is freed;
   no VM-managed page is preloaded, so later VM accesses require normal refill. */
static void preload_preflight(void) {
    volatile uint32_t *const pteh = (void *)0xff000000u;
    volatile uint32_t *const ptel = (void *)0xff000004u;
    volatile uint32_t *const mmucr = (void *)0xff000010u;
    unsigned vpn = ADDRESS >> 12;
    const mmupage_t *page = &context->sub[vpn / MMU_SUB_PAGES]->page[vpn % MMU_SUB_PAGES];
    irq_mask_t old = irq_disable();
    uint32_t saved_h = *pteh, saved_l = *ptel, saved_control = *mmucr;
    *mmucr = saved_control & ~(63u << 10);
    *pteh = page->pteh | (unsigned)context->asid;
    *ptel = page->ptel;
    __asm__ volatile("ldtlb");
    *mmucr = saved_control;
    *pteh = saved_h;
    *ptel = saved_l;
    __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop");
    irq_restore(old);
}

/* This preflight uses separately owned low-level backing before VM admission.
   It distinguishes general translated execution from SQ-only emulation. */
static int translated_preflight(void) {
    uint32_t *backing = aligned_alloc(BYTES, BYTES);
    if(!backing) return -1;
    backing[0] = COOKIE;
    dcache_purge_range((uintptr_t)backing, BYTES);
    int rc = mmu_page_map_ex(context, ADDRESS >> 12,
            ((uintptr_t)backing & MEM_AREA_CACHE_MASK) >> 12, 1,
            MMU_KERNEL_RDWR, MMU_CACHE_BACK, false, true);
    if(rc < 0) { free(backing); return -1; }
    printf("VM1 preflight backing=%08lx\n", (unsigned long)backing);
    preload_preflight();
    int translated = data[0] == COOKIE;
    if(translated) {
        uint32_t *replacement = aligned_alloc(BYTES, BYTES);
        if(!replacement) {
            if(mmu_page_unmap_ex(context, ADDRESS >> 12, 1) < 0)
                arch_panic("resident-vm preflight retirement failed");
            free(backing);
            return -1;
        }
        replacement[0] = ~COOKIE;
        dcache_purge_range((uintptr_t)replacement, BYTES);
        rc = mmu_page_map_ex(context, ADDRESS >> 12,
                ((uintptr_t)replacement & MEM_AREA_CACHE_MASK) >> 12, 1,
                MMU_KERNEL_RDWR, MMU_CACHE_BACK, false, true);
        if(rc == 0) {
            uint32_t observed = data[0];
            printf("VM1 preflight remap expected=%08lx observed=%08lx\n",
                   (unsigned long)~COOKIE, (unsigned long)observed);
            VERIFY(observed == ~COOKIE);
            if(observed != ~COOKIE)
                translated = -1;
            /* The replacement retired the old mapping before freeing it. */
            free(backing);
            backing = replacement;
        }
        else {
            free(replacement);
            VERIFY(rc == 0);
            translated = -1;
        }
    }
    if(mmu_page_unmap_ex(context, ADDRESS >> 12, 1) < 0)
        arch_panic("resident-vm preflight retirement failed");
    free(backing);
    return translated;
}

static void child(void *arg) {
    unsigned id = (uintptr_t)arg;
    VERIFY(thd_get_current() == carrier && mmu_cxt_current == context);
    VERIFY(tls_cookie == COOKIE && &errno == carrier_errno && errno == EDOM);
    data[0] = COOKIE + id;
    last_value = COOKIE + id;
    unsigned before = samples[id];
    uint64_t deadline = timer_ms_gettime64() + 2000;
    active = id;
    /* No explicit yield here: progress by the other carrier proves preemption. */
    while(samples[id] - before < 2 && timer_ms_gettime64() < deadline) {
        if(data[0] != COOKIE + id) { ++failures; break; }
    }
    active = -1;
    VERIFY(samples[id] - before >= 2);
    VERIFY(fiber_switch(main_fiber) == 0);
    VERIFY(thd_get_current() == carrier && mmu_cxt_current == context);
    VERIFY(tls_cookie == COOKIE && &errno == carrier_errno && errno == EDOM);
    VERIFY(data[0] == last_value);
    data[0] = ++last_value;
}

static void *other_carrier(void *unused) {
    (void)unused;
    tls_cookie = ~COOKIE;
    while(!stop) {
        int id = active;
        if(id >= 0) {
            if(mmu_cxt_current != context || thd_get_current() == carrier)
                ++helper_failures;
            ++data[1];
            ++samples[id];
        }
        thd_pass();
    }
    if(tls_cookie != ~COOKIE) ++helper_failures;
    return NULL;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    vm_space_t *space = NULL;
    void *region = NULL;
    kthread_t *helper = NULL;
    mmucontext_t *previous = mmu_cxt_current;
    int translated = -1;
    vm_info_t info;
    context = mmu_context_create(73);
    REQUIRE(context && mmu_enabled());
    {
        irq_mask_t old = irq_disable();
        mmu_use_table(context);
        mmu_switch_context(context);
        irq_restore(old);
    }
    translated = translated_preflight();
    REQUIRE(translated >= 0);
    printf("VM1 preflight translation=%d\n", translated);
    space = vm_space_create(context, (void *)ADDRESS, BYTES * 3);
    REQUIRE(space);
    VERIFY(!vm_space_create(context, (void *)0x7c000000u, BYTES) && errno == EINVAL);
    REQUIRE(vm_reserve(space, NULL, BYTES * 3, &region) == 0);
    REQUIRE(vm_commit(space, region, BYTES * 3, VM_READWRITE) == 0);
    VERIFY(vm_commit(space, region, BYTES, VM_READ) == -1 && errno == EALREADY);
    REQUIRE(vm_query(space, region, &info) == 0);
    VERIFY(info.state == VM_COMMITTED && info.run_size == BYTES * 3);
    {
        irq_mask_t old = irq_disable();
        int rc = vm_query(space, region, &info);
        int error = errno;
        irq_restore(old);
        VERIFY(rc == -1 && error == EPERM);
    }
    if(translated) {
        for(unsigned page = 0; page < 3; ++page) {
            unsigned bad = 0, first = 0;
            uint32_t first_value = 0;
            for(unsigned i = 0; i < BYTES / sizeof(*data); ++i) {
                uint32_t value = data[page * BYTES / sizeof(*data) + i];
                if(value) {
                    if(!bad) { first = i; first_value = value; }
                    ++bad;
                }
            }
            printf("VM1 zero page=%u bad=%u first=%u value=%08lx physical=%08x\n",
                   page, bad, first, (unsigned long)first_value,
                   mmu_virt_to_phys(context, (ADDRESS >> 12) + page) << 12);
            VERIFY(bad == 0);
        }
        carrier = thd_get_current();
        carrier_errno = &errno;
        tls_cookie = COOKIE;
        main_fiber = fiber_attach();
        REQUIRE(main_fiber);
        for(unsigned i = 0; i < 2; ++i) {
            children[i] = fiber_create(stacks[i], sizeof(stacks[i]), child, (void *)(uintptr_t)i);
            REQUIRE(children[i]);
        }
        helper = thd_create(false, other_carrier, NULL);
        REQUIRE(helper);
        for(unsigned round = 0; round < 2; ++round)
            for(unsigned i = 0; i < 2; ++i) {
                errno = EDOM;
                VERIFY(fiber_switch(children[i]) == 0);
            }
        stop = true;
        REQUIRE(thd_join(helper, NULL) == 0);
        helper = NULL;
        for(unsigned i = 0; i < 2; ++i) {
            REQUIRE(fiber_destroy(children[i]) == 0);
            children[i] = NULL;
        }
        VERIFY(data[1] >= 4 && helper_failures == 0);
    }
    REQUIRE(vm_protect(space, region, BYTES, VM_READ) == 0);
    if(translated) VERIFY(data[0] == last_value);
    /* Deliberate write faults are not recovered by a new production handler. */
    REQUIRE(vm_protect(space, region, BYTES, VM_READWRITE) == 0);
    REQUIRE(vm_decommit(space, region, BYTES) == 0);
    REQUIRE(vm_query(space, region, &info) == 0);
    VERIFY(info.state == VM_RESERVED && info.run_size == BYTES);
    REQUIRE(vm_commit(space, region, BYTES, VM_READWRITE) == 0);
    if(translated) {
        VERIFY(data[0] == 0 && data[1] == 0);
        data[0] = COOKIE;
        VERIFY(data[0] == COOKIE);
    }

cleanup:
    stop = true;
    if(helper) thd_join(helper, NULL);
    for(unsigned i = 0; i < 2; ++i)
        if(children[i]) VERIFY(fiber_destroy(children[i]) == 0);
    if(region) VERIFY(vm_release(space, region) == 0);
    if(space) VERIFY(vm_space_destroy(space) == 0);
    if(context) {
        irq_mask_t old = irq_disable();
        mmu_use_table(previous);
        if(previous) mmu_switch_context(previous);
        irq_restore(old);
        mmu_context_destroy(context);
    }
    printf("VM1 RESULT failures=%u translation=%d preempt=%u,%u data=%s\n",
           failures + helper_failures, translated, samples[0], samples[1],
           translated == 1 ? "EXECUTED" : "SKIPPED");
    fflush(stdout);
    return failures || helper_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
