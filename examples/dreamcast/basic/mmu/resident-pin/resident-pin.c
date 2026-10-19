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
#include <string.h>

#define ADDRESS 0x101ff000u
#define BYTES 4096u
#define COOKIE 0x564d3337u
static volatile uint32_t *const data = (void *)ADDRESS;
static mmucontext_t *context;
static vm_space_t *space;
static vm_pin_t *pin;
static kfiber_t *parent;
static alignas(32) unsigned char stack[8192];
static unsigned failures, geometry, yields, abandoned, transfers;
static void failed(unsigned line, const char *expression) {
    int saved = errno;
    ++failures;
    printf("VM3PIN CHECK line=%u errno=%d: %s\n", line, saved, expression);
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
    printf("VM3PIN preflight backing=%08lx\n", (unsigned long)backing);
    preload_preflight();
    int translated = data[0] == COOKIE;
    if(translated) {
        uint32_t *replacement = aligned_alloc(BYTES, BYTES);
        if(!replacement) {
            if(mmu_page_unmap_ex(context, ADDRESS >> 12, 1) < 0)
                arch_panic("resident-pin preflight retirement failed");
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
            printf("VM3PIN preflight remap expected=%08lx observed=%08lx\n",
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
        arch_panic("resident-pin preflight retirement failed");
    free(backing);
    return translated;
}

/* Compare all requested bytes with translated CPU reads and the manager's
   existing page-table coordinates, without constructing application aliases. */
static void check_runs(void) {
    vm_pin_info_t info;
    if(vm_pin_query(pin, &info)) { failed(__LINE__, "query"); return; }
    VERIFY(info.context == context && info.asid == 75);
    size_t covered = 0, n = 99;
    VERIFY(!vm_pin_runs(pin, 0, NULL, 0, &n) && n == 0);
    for(size_t cursor = 0; cursor < info.run_count; ++cursor) {
        vm_phys_run_t run;
        if(vm_pin_runs(pin, cursor, &run, 1, &n)) { failed(__LINE__, "runs"); return; }
        VERIFY(n == 1 && run.offset == covered && run.bytes);
        for(size_t i = 0; i < run.bytes; ++i) {
            uintptr_t va = (uintptr_t)info.address + run.offset + i;
            int page = mmu_virt_to_phys(context, va / BYTES);
            VERIFY(page >= 0 && run.physical + i == (uintptr_t)page * BYTES + va % BYTES);
            VERIFY(*(volatile unsigned char *)va == (unsigned char)((va - ADDRESS) * 17 + 3));
        }
        covered += run.bytes;
    }
    VERIFY(covered == info.bytes);
    VERIFY(!vm_pin_runs(pin, info.run_count, NULL, 0, &n) && n == 0);
    ++geometry;
}
static void child(void *unused) {
    (void)unused;
    check_runs(); ++yields;
    VERIFY(!fiber_switch(parent));
    check_runs(); ++yields;
}
static void parked_child(void *unused) {
    (void)unused;
    check_runs();
    VERIFY(!fiber_switch(parent));
    failed(__LINE__, "abandoned continuation resumed");
}
static void *producer(void *unused) {
    (void)unused;
    VERIFY(!vm_pin_acquire(space, (void *)(ADDRESS + 31), 2 * BYTES + 7,
                           VM_PIN_READWRITE, &pin));
    return NULL; /* join acknowledges resident publication and producer retirement */
}
static void *reaper(void *unused) {
    (void)unused;
    VERIFY(!vm_pin_release(pin)); pin = NULL;
    return NULL;
}
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("VM3PIN BEGIN");
    mmucontext_t *previous = mmu_cxt_current;
    void *region = NULL;
    kfiber_t *c = NULL;
    kthread_t *t = NULL;
    int translated = -1;
    context = mmu_context_create(75);
    REQUIRE(context && mmu_enabled());
    irq_mask_t mask = irq_disable();
    mmu_use_table(context); mmu_switch_context(context); irq_restore(mask);
    translated = translated_preflight();
    REQUIRE(translated == 1);
    space = vm_space_create(context, (void *)ADDRESS, 3 * BYTES);
    REQUIRE(space && !vm_reserve(space, NULL, 3 * BYTES, &region));
    REQUIRE(!vm_commit(space, region, 3 * BYTES, VM_READWRITE));
    for(size_t i = 0; i < 3 * BYTES; ++i)
        ((volatile unsigned char *)region)[i] = (unsigned char)(i * 17 + 3);
    REQUIRE(!vm_pin_acquire(space, (void *)(ADDRESS + 31), 2 * BYTES + 7,
                            VM_PIN_READWRITE, &pin));
    check_runs();
    parent = fiber_attach_ex(KFIBER_ATTACH_MATH_CONTEXT);
    REQUIRE(parent);
    c = fiber_create(stack, sizeof(stack), child, NULL); REQUIRE(c);
    REQUIRE(!fiber_switch(c));
    VERIFY(vm_decommit(space, region, 3 * BYTES) < 0 && errno == EBUSY);
    VERIFY(vm_protect(space, region, BYTES, VM_READWRITE) < 0 && errno == EBUSY);
    REQUIRE(!fiber_switch(c));
    VERIFY(fiber_get_state(c) == KFIBER_STATE_FINISHED);
    REQUIRE(!fiber_destroy(c)); c = NULL;
    c = fiber_create(stack, sizeof(stack), parked_child, NULL); REQUIRE(c);
    REQUIRE(!fiber_switch(c));
    /* Parent owns the lease and retires this borrowed continuation by destroy. */
    REQUIRE(!fiber_destroy(c)); c = NULL; ++abandoned;
    check_runs();
    REQUIRE(!vm_pin_release(pin)); pin = NULL;
    t = thd_create(false, producer, NULL); REQUIRE(t);
    REQUIRE(!thd_join(t, NULL)); t = NULL; REQUIRE(pin); ++transfers;
    check_runs();
    t = thd_create(false, reaper, NULL); REQUIRE(t);
    REQUIRE(!thd_join(t, NULL)); t = NULL; REQUIRE(!pin); ++transfers;
    REQUIRE(!vm_protect(space, region, BYTES, VM_READ));
    vm_pin_t *sentinel = (void *)123;
    VERIFY(vm_pin_acquire(space, region, 1, VM_PIN_WRITE, &sentinel) < 0 && errno == EACCES);
    VERIFY(sentinel == (void *)123);
cleanup:
    if(t) thd_join(t, NULL);
    if(c) VERIFY(!fiber_destroy(c));
    if(pin) { VERIFY(!vm_pin_release(pin)); pin = NULL; }
    if(region) VERIFY(!vm_release(space, region));
    if(space) VERIFY(!vm_space_destroy(space));
    if(context) {
        irq_mask_t saved = irq_disable(); mmu_use_table(previous);
        if(previous) mmu_switch_context(previous);
        irq_restore(saved); mmu_context_destroy(context);
    }
    printf("VM3PIN RESULT failures=%u translation=%d geometry=%u yields=%u abandoned=%u transfers=%u\n",
           failures, translated, geometry, yields, abandoned, transfers);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
