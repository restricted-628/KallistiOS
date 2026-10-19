/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <kos.h>
#include <kos/vm.h>
#include <kos/fiber.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ADDRESS 0x103ff000u
#define COOKIE 0xaabbccddu
/* Layout is shared with the straight-line assembly fixture. */
typedef struct {
    uint32_t r[16], pr, gbr, vbr, mach, macl, sr, fpul, fpscr, fr[16], xf[16];
} state_t;
state_t original, before, after; _Static_assert(sizeof(state_t) == 224, "assembly snapshot layout");
/* Probe-only observation of the exception return-frame identity. */
extern irq_context_t *irq_srt_addr;
#define DECLARE(name) extern void fault_##name(void *); extern const char fault_##name##_pc[]
DECLARE(load); DECLARE(store); DECLARE(delay_load); DECLARE(delay_store);
static void (*const accessors[])(void *) = {fault_load, fault_store, fault_delay_load, fault_delay_store};
static const char *const pcs[] = {fault_load_pc, fault_store_pc, fault_delay_load_pc, fault_delay_store_pc};
static vm_space_t *space;
static mmucontext_t *context;
static kthread_t *carrier;
static kthread_key_t key;
static _Thread_local unsigned tls_cookie;
static alignas(32) unsigned char stack[16384], sibling_stack[8192];
static volatile unsigned sibling_runs, filter_order;
static void sibling(void *arg) { (void)arg; ++sibling_runs; }
static void filter_global(irq_t e, irq_context_t *f, void *a) {
    (void)f; (void)a; if(e == EXC_TRAPA) filter_order = filter_order * 10 + 1;
}
static void filter_specific(irq_t e, irq_context_t *f, void *a) {
    (void)f; (void)a; if(e == EXC_TRAPA) filter_order = filter_order * 10 + 2;
}
static unsigned failures, cases, state_checks, zeros, guards, child_cases, observer_cases;
static volatile unsigned seen;
static volatile uint32_t observed_pc, observed_sr, observed_sp, observed_event;
static int *errno_address;
static struct _reent *reent_address;
static void failed(unsigned line) { ++failures; printf("VM2REC CHECK line=%u\n", line); }
#define CHECK(x) do { if(!(x)) failed(__LINE__); } while(0)
#define REQUIRE(x) do { if(!(x)) { failed(__LINE__); return; } } while(0)
static void observer(irq_t event, irq_context_t *f, void *arg) {
    (void)arg;
    if((event == 0x40 || event == 0x60) && *(volatile uint32_t *)0xff00000cu == ADDRESS) {
        ++seen; observed_pc = f->pc; observed_sr = f->sr;
        observed_sp = f->r[15]; observed_event = event;
    }
}
static void run_cases(void *arg) {
    unsigned child = (uintptr_t)arg;
    for(unsigned hook = 0; hook < 2; ++hook) {
        for(unsigned guard = 0; guard < 2; ++guard) {
            for(unsigned op = 0; op < 4; ++op) {
                void *page; vm_fault_t *ticket; vm_fault_info_t info; vm_info_t region;
                bool write = op & 1;
                vm_prot_t prot = write ? VM_READWRITE : VM_READ;
                REQUIRE(!vm_reserve(space, (void *)ADDRESS, 4096, &page));
                if(guard) {
                    REQUIRE(!vm_commit(space, page, 4096, VM_READWRITE));
                    *(volatile uint32_t *)page = COOKIE;
                    REQUIRE(!vm_protect(space, page, 4096, prot));
                }
                REQUIRE(!vm_fault_arm(space, page, guard ? VM_FAULT_GUARD : VM_FAULT_ZERO_COMMIT, prot, &ticket));
                REQUIRE(!vm_query(space, page, &region));
                CHECK(region.state == (guard ? VM_GUARDED : VM_RESERVED));
                seen = 0;
                irq_set_global_handler(hook ? observer : NULL, NULL);
                void *wait_obj = carrier->wait_obj;
                const char *wait_msg = carrier->wait_msg;
                uint64_t wait_timeout = carrier->wait_timeout;
                irq_context_t *return_frame = irq_srt_addr;
                kfiber_t *current_fiber = fiber_current();
                unsigned siblings = sibling_runs;
                errno = EDOM;
                accessors[op](page);
                int saved_errno = errno;
                irq_set_global_handler(NULL, NULL);
                CHECK(carrier->wait_obj == wait_obj && carrier->wait_msg == wait_msg && carrier->wait_timeout == wait_timeout);
                CHECK(irq_srt_addr == return_frame && fiber_current() == current_fiber && sibling_runs == siblings);
                CHECK(saved_errno == EDOM && &errno == errno_address);
                CHECK(_impure_ptr == reent_address && tls_cookie == COOKIE);
                CHECK(kthread_getspecific(key) == carrier && thd_get_current() == carrier);
                CHECK(mmu_cxt_current == context && (*(volatile uint32_t *)0xff000000u & 255) == 74);
                CHECK(*(volatile uint32_t *)page == (write ? 34u : (guard ? COOKIE : 0)));
                if(!write) before.r[2] = guard ? COOKIE : 0;
                if(memcmp(&before, &after, sizeof(before))) {
                    const uint32_t *b = (const void *)&before, *a = (const void *)&after;
                    for(unsigned i = 0; i < sizeof(before)/4; ++i)
                        if(b[i] != a[i]) printf("VM2REC STATE word=%u before=%08lx after=%08lx\n", i, (unsigned long)b[i], (unsigned long)a[i]);
                    failed(__LINE__);
                }
                else ++state_checks;
                REQUIRE(!vm_fault_query(ticket, &info));
                CHECK(info.state == VM_FAULT_RESOLVED && info.snapshot_valid);
                CHECK(info.snapshot.event == (write ? 0x60u : 0x40u));
                CHECK(info.snapshot.pc == (uintptr_t)pcs[op] && info.snapshot.address == ADDRESS);
                CHECK(info.snapshot.sp == before.r[15] && info.snapshot.sr == before.sr);
                CHECK(info.snapshot.table == (uintptr_t)context && info.snapshot.asid == 74);
                CHECK(info.snapshot.carrier == (uintptr_t)carrier && info.snapshot.outcome == VM_FAULT_RETRY);
                CHECK((info.snapshot.mmucr & 1) && info.snapshot.access == (write ? VM_FAULT_WRITE : VM_FAULT_READ_OR_FETCH));
                CHECK(seen == hook);
                if(hook) CHECK(observed_pc == info.snapshot.pc && observed_sr == info.snapshot.sr &&
                               observed_sp == info.snapshot.sp && observed_event == info.snapshot.event);
                if(child) CHECK(before.r[15] >= (uintptr_t)stack && before.r[15] < (uintptr_t)(stack + sizeof(stack)));
                /* A second ordinary access does not consume another ticket. */
                uint32_t again = *(volatile uint32_t *)page;
                CHECK(again == (write ? 34u : (guard ? COOKIE : 0)));
                vm_fault_info_t repeated;
                REQUIRE(!vm_fault_query(ticket, &repeated));
                CHECK(!memcmp(&info, &repeated, sizeof(info)));
                CHECK(vm_decommit(space, page, 4096) < 0 && errno == EBUSY);
                irq_mask_t mask = irq_disable();
                int rc = vm_fault_query(ticket, &repeated), error = errno;
                irq_restore(mask);
                CHECK(rc < 0 && error == EPERM);
                REQUIRE(!vm_fault_release(ticket));
                REQUIRE(!vm_release(space, page));
                ++cases; zeros += !guard; guards += guard; child_cases += child; observer_cases += hook;
                printf("VM2REC CASE child=%u hook=%u guard=%u op=%u failures=%u\n", child, hook, guard, op, failures);
            }
        }
    }
}
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("VM2REC BEGIN\n");
    mmucontext_t *previous = mmu_cxt_current;
    context = mmu_context_create(74);
    if(!context || !mmu_enabled()) { printf("VM2REC SETUP FAIL\n"); return 1; }
    irq_mask_t mask = irq_disable(); mmu_use_table(context); mmu_switch_context(context); irq_restore(mask);
    space = vm_space_create(context, (void *)ADDRESS, 4096);
    if(!space) { printf("VM2REC SETUP FAIL space\n"); return 1; }
    carrier = thd_get_current(); errno_address = &errno; reent_address = _impure_ptr; tls_cookie = COOKIE;
    if(kthread_key_create(&key, NULL) || kthread_setspecific(key, carrier)) return 1;
    /* Trap fixture checks unchanged global-before-specific order and the
       documented global-only filter. Mask preemption while handlers are swapped. */
    irq_cb_t saved_trap = irq_get_handler(EXC_TRAPA);
    irq_cb_t saved_global = irq_get_global_handler();
    mask = irq_disable();
    irq_set_global_handler(filter_global, NULL);
    irq_set_handler(EXC_TRAPA, filter_specific, NULL);
    __asm__ volatile("trapa #127" : : : "memory");
    unsigned order = filter_order;
    filter_order = 0; irq_set_handler(EXC_TRAPA, NULL, NULL);
    __asm__ volatile("trapa #127" : : : "memory");
    unsigned filtered = filter_order;
    irq_set_handler(EXC_TRAPA, saved_trap.hdl, saved_trap.data);
    irq_set_global_handler(saved_global.hdl, saved_global.data);
    irq_restore(mask);
    CHECK(order == 12 && filtered == 1);
    run_cases(NULL);
    kfiber_t *main_fiber = fiber_attach_ex(KFIBER_ATTACH_MATH_CONTEXT);
    kfiber_t *other = fiber_create(sibling_stack, sizeof(sibling_stack), sibling, NULL);
    kfiber_t *child = fiber_create(stack, sizeof(stack), run_cases, (void *)1);
    if(!main_fiber || !child || !other) { printf("VM2REC SETUP FAIL fiber\n"); return 1; }
    CHECK(!fiber_switch(child)); CHECK(fiber_get_state(child) == KFIBER_STATE_FINISHED);
    CHECK(sibling_runs == 0); CHECK(!fiber_switch(other)); CHECK(sibling_runs == 1);
    CHECK(!fiber_destroy(other)); CHECK(!fiber_destroy(child)); CHECK(!kthread_key_delete(key));
    CHECK(!vm_space_destroy(space));
    mask = irq_disable(); mmu_use_table(previous); if(previous) mmu_switch_context(previous); irq_restore(mask);
    mmu_context_destroy(context);
    printf("VM2REC RESULT failures=%u cases=%u state=%u zero=%u guard=%u child=%u observer=%u\n",
           failures, cases, state_checks, zeros, guards, child_cases, observer_cases);
    return failures ? 1 : 0;
}
