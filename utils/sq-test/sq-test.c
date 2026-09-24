/* Exercise the production SQ driver with fake mutex/MMU and mapped registers.
   This does not model bus timing, cache coherency, or SH-4 instructions. */
#include "sq-test-shim.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../../kernel/arch/dreamcast/hardware/sq.c"

unsigned assertion_failures;
static unsigned checks, map_calls, fast_calls, flush_calls;
static bool in_irq, mmu_on, fail_lock;
static uintptr_t mapped;
static kthread_t owner = {1}, other = {2};
kthread_t *thd_current = &owner;
static uintptr_t fast_dest[4], fast_src[4];
static size_t fast_lines[4];
static unsigned fast_qacr[4];

#define CHECK(c) do { ++checks; if(!(c)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while(0)

int mutex_lock(mutex_t *m) {
    if(fail_lock || (m->count && m->holder != thd_current)) {
        errno = EBUSY;
        return -1;
    }
    m->holder = thd_current;
    ++m->count;
    return 0;
}
int mutex_unlock(mutex_t *m) {
    CHECK(m->count && m->holder == thd_current);
    if(!--m->count)
        m->holder = NULL;
    return 0;
}
bool irq_inside_int(void) { return in_irq; }
bool mmu_enabled(void) { return mmu_on; }
void mmu_set_sq_addr(void *p) { mapped = (uintptr_t)p; ++map_calls; }
void sq_flush(void *p) { (void)p; ++flush_calls; }
void *sq_fast_cpy(void *dest, const void *src, size_t lines) {
    CHECK(fast_calls < 4);
    fast_dest[fast_calls] = (uintptr_t)dest;
    fast_src[fast_calls] = (uintptr_t)src;
    fast_lines[fast_calls] = lines;
    fast_qacr[fast_calls++] = QACR0;
    return dest;
}

static void *map_at(uintptr_t address, size_t bytes) {
    /* No MAP_FIXED: refuse an occupied address instead of replacing it. */
    void *p = mmap((void *)address, bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(p == (void *)address);
    return p;
}

static void ownership(bool mode) {
    mmu_on = mode;
    uintptr_t a = 0x8c001000u, b = 0xa0810000u;
    uintptr_t mask = mode ? 0xfffffu : 0x3ffffffu;
    CHECK((uintptr_t)sq_lock((void *)a) == (0xe0000000u | (a & mask)));
    CHECK((mode ? mapped : QACR0) == (mode ? a : 12));
    CHECK(sq_lock((void *)b) != NULL);
    CHECK((mode ? mapped : QACR0) == (mode ? b : 0));
    sq_unlock();
    CHECK((mode ? mapped : QACR0) == (mode ? a : 12));
    for(unsigned i = 1; i < 8; ++i)
        CHECK(sq_lock((void *)a) != NULL);
    unsigned old_maps = map_calls;
    CHECK(!sq_lock((void *)b) && errno == EOVERFLOW);
    CHECK(sq_mutex.count == 8 && map_calls == old_maps);
    CHECK((mode ? mapped : QACR0) == (mode ? a : 12));
    thd_current = &other;
    sq_unlock();
    CHECK(sq_mutex.count == 8 && sq_mutex.holder == &owner);
    CHECK(assertion_failures == 1);
    thd_current = &owner;
    in_irq = true;
    CHECK(!sq_lock((void *)b) && errno == EPERM);
    sq_unlock();
    CHECK(sq_mutex.count == 8 && assertion_failures == 2);
    in_irq = false;
    sq_unlock();
    mmu_on = !mode;
    CHECK(!sq_lock((void *)b) && errno == EBUSY);
    CHECK(sq_mutex.count == 7 && map_calls == old_maps + (mode ? 1 : 0));
    mmu_on = mode;
    while(sq_mutex.count)
        sq_unlock();
    assertion_failures = 0;
    sq_unlock();
    CHECK(!sq_mutex.count);
}

int main(void) {
    void *regs = map_at(0xff000000u, 4096);
    void *queues = map_at(0xe0000000u, 0x4000000u);
    static _Alignas(32) uint32_t input[16];
    CHECK(!sq_lock(NULL) && errno == EINVAL);
    CHECK(!sq_lock((void *)0x8c000001u) && errno == EINVAL);
    CHECK(!sq_cpy((void *)0x8c000000u, input, 33) && errno == EINVAL);
    CHECK(!sq_cpy((void *)0x8c000000u, (char *)input + 1, 32));
    CHECK(!sq_set32((void *)0x8c000001u, 0, 32));
    CHECK(!sq_set32((void *)(UINTPTR_MAX - 31), 0, 64));
    CHECK(sq_cpy(NULL, NULL, 0) == NULL && !sq_mutex.count);
    CHECK(sq_set32(NULL, 0, 0) == NULL && !sq_mutex.count);
    fail_lock = true;
    CHECK(!sq_cpy((void *)0x8c000000u, input, 32) && errno == EBUSY);
    CHECK(!sq_set32((void *)0x8c000000u, 0, 32) && errno == EBUSY);
    CHECK(!sq_mutex.count && !fast_calls && !flush_calls);
    fail_lock = false;
    ownership(false);
    ownership(true);
    mmu_on = false;
    CHECK(sq_cpy((void *)0x8fffffe0u, input, 64) == (void *)0x8fffffe0u);
    CHECK(fast_calls == 2 && fast_lines[0] == 1 && fast_lines[1] == 1);
    CHECK(fast_dest[0] == 0xe3ffffe0u && fast_dest[1] == 0xe0000000u);
    CHECK(fast_qacr[0] == 12 && fast_qacr[1] == 16);
    CHECK(fast_src[1] - fast_src[0] == 32);
    CHECK(sq_set32((void *)0x8fffffe0u, 0x12345678u, 64));
    CHECK(flush_calls == 2 && QACR0 == 16);
    CHECK(*(uint32_t *)0xe3ffffe0u == 0x12345678u);
    CHECK(*(uint32_t *)0xe0000000u == 0x12345678u);
    for(uintptr_t offset = 0; offset < 0x4000000; offset += 32) {
        size_t n = sq_batch_lines((void *)(0x8c000000u + offset), 0x9000);
        CHECK(n && n <= 0x8000 && offset + n * 32 <= 0x4000000);
    }
    sq_wait();
    CHECK(*(uint32_t *)0xe0000000u == 0 && *(uint32_t *)0xe0000020u == 0);
    CHECK(!sq_mutex.count && !assertion_failures);
    CHECK(munmap(queues, 0x4000000) == 0);
    CHECK(munmap(regs, 4096) == 0);
    printf("SQ ownership: %u checks passed\n", checks);
    return 0;
}
