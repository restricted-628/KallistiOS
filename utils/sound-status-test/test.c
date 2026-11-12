/* Exact native getter/helpers with instrumented bus, context and scheduler. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __ARCH_TYPES_H
typedef uint8_t uint8;
typedef uint32_t uint32;
typedef uint32_t irq_mask_t;
#define __KOS_CDEFS_H
#include <dc/sound/sound.h>
#include "../../kernel/arch/dreamcast/sound/arm/aica_cmd_iface.h"
#define SPU_RAM_UNCACHED_BASE 0xa0800000u
#define THD_MODE_PREEMPT 1
static int initted = 1;
static uint32_t driver_features = SND_DRIVER_FEATURE_CHANNEL_CONTROL;
typedef struct { uint32_t irq_state, dma_suspend[4]; } g2_ctx_t;
static uint32_t sr, entry_sr, dma[4], entry_dma[4];
static unsigned depth, entry_depth, locks, unlocks, reads, attempts, checks, yields;
static unsigned sequence_pending, mode;
static int inside_irq, has_current, thread_mode;
static aica_channel_status_ext_t memory;
static void publish(uint32_t sequence) {
    memory.sequence = sequence; memory.position = sequence * 10;
    memory.config.freq = 22050; memory.configured = memory.playing = 1;
}
static g2_ctx_t g2_lock(void) {
    g2_ctx_t saved = {sr,{0}};
    memcpy(saved.dma_suspend, dma, sizeof(dma));
    sr |= 0xf0; for(unsigned i = 0; i < 4; ++i) dma[i] = 1;
    ++depth; ++locks; return saved;
}
static void g2_unlock(g2_ctx_t saved) {
    assert(depth == entry_depth + 1); --depth;
    memcpy(dma, saved.dma_suspend, sizeof(dma)); sr = saved.irq_state;
    assert(sr == entry_sr && !memcmp(dma, entry_dma, sizeof(dma)));
    ++unlocks;
    if(mode == 1 && unlocks == 1) publish(2);
}
#ifdef EXPECT_OLD
static void cleanup(g2_ctx_t *saved) { g2_unlock(*saved); }
#define g2_lock_scoped() g2_ctx_t scope __attribute__((cleanup(cleanup))) = g2_lock()
#else
static int irq_inside_int(void) { return inside_irq; }
static void *thd_get_current(void) { return has_current ? &memory : NULL; }
static int thd_get_mode(void) { return thread_mode; }
static void thd_pass(void) {
    assert(!inside_irq && has_current && thread_mode == THD_MODE_PREEMPT);
    assert(!(sr & 0x100000f0u) && sr == entry_sr && depth == entry_depth);
    assert(locks == unlocks && !memcmp(dma, entry_dma, sizeof(dma)));
    ++yields;
    if(mode == 5) publish(2);
    errno = ERANGE; /* A void scheduler operation must not leak incidental errno. */
}
#endif
static void g2_fifo_wait(void) { assert(depth == entry_depth + 1); }
static uint32_t g2_read_32_raw(uint32_t address) {
    assert(depth == entry_depth + 1); ++reads;
    uint32_t offset = address - SPU_RAM_UNCACHED_BASE - AICA_CHANNEL_STATUS(3);
    assert(offset < sizeof(memory) && !(offset & 3));
    if(offset == 0) {
        if(memory.sequence & 1) ++attempts;
        else if(!sequence_pending) { ++attempts; sequence_pending = 1; }
        else {
            sequence_pending = 0;
            if(mode == 3 || (mode == 4 && attempts == 1)) publish(memory.sequence + 2);
        }
    }
    return ((uint32_t *)&memory)[offset / 4];
}
#include "exact.inc"
#define CHECK(x) do { ++checks; assert(x); } while(0)
static void reset(unsigned next_mode, uint32_t mask, unsigned nested) {
    sequence_pending = 0; mode = next_mode;
    locks = unlocks = reads = attempts = yields = 0;
    entry_sr = sr = mask | (nested ? 0xf0 : 0);
    entry_depth = depth = nested;
    for(unsigned i = 0; i < 4; ++i) entry_dma[i] = dma[i] = (i == 2 ? 1 : 0);
    memset(&memory, 0, sizeof(memory));
    publish(mode == 1 || mode == 2 || mode == 5 ? 1 : 2);
    initted = 1; driver_features = SND_DRIVER_FEATURE_CHANNEL_CONTROL;
    inside_irq = 0; has_current = 1; thread_mode = THD_MODE_PREEMPT;
}
static void restored(void) {
    CHECK(depth == entry_depth && sr == entry_sr);
    CHECK(!memcmp(dma, entry_dma, sizeof(dma)) && locks == unlocks);
}
int main(void) {
    snd_channel_status_ex_t out, zero;
    memset(&zero, 0, sizeof(zero));
#ifdef EXPECT_OLD
    reset(5, 0, 0); memset(&out, 0xa5, sizeof(out));
    CHECK(snd_channel_get_status_ex(3, &out) == -1 && errno == EAGAIN);
    CHECK(attempts == 8 && locks == EXPECTED_LOCKS && unlocks == EXPECTED_LOCKS && !yields);
    CHECK(!memcmp(&out, &zero, sizeof(out))); restored();
    puts("BEFORE: eight odd reads exhaust without modeled writer scheduling opportunity");
#else
    /* Every IMASK combination, BL, IRQ, live/stale/missing current, mode, nesting. */
    for(unsigned mask = 0; mask < 16; ++mask)
    for(unsigned bl = 0; bl < 2; ++bl)
    for(unsigned nested = 0; nested < 2; ++nested)
    for(unsigned context = 0; context < 5; ++context) {
        reset(5, mask * 0x10u | (bl ? 0x10000000u : 0), nested);
        if(context == 1) inside_irq = 1;
        if(context == 2) has_current = 0;
        if(context == 3) thread_mode = -1; /* stale current after shutdown */
        if(context == 4) thread_mode = 0; /* unsupported cooperative mode */
        bool allowed = !(entry_sr & 0x100000f0u) && context == 0;
        errno = EIO; memset(&out, 0xa5, sizeof(out));
        int result = snd_channel_get_status_ex(3, &out);
        if(allowed) {
            CHECK(result == 0 && yields == 1 && attempts == 2);
            CHECK(out.sequence == 2 && out.position == 20 && errno == EIO);
            CHECK(out.config.sample_rate == 22050);
        } else {
            CHECK(result == -1 && errno == EAGAIN && attempts == 8 && !yields);
            CHECK(!memcmp(&out, &zero, sizeof(out)));
        }
        restored();
    }
    reset(5, 0, 0); thread_mode = 99;
    CHECK(snd_channel_get_status_ex(3, &out) == -1 && errno == EAGAIN && !yields); restored();
    reset(0, 0, 0); errno = EIO;
    CHECK(snd_channel_get_status_ex(3, &out) == 0 && locks == 1 && !yields && errno == EIO); restored();
    reset(1, 0, 0);
    CHECK(snd_channel_get_status_ex(3, &out) == 0 && attempts == 2 && yields == 1); restored();
    for(unsigned m = 2; m <= 3; ++m) {
        reset(m, 0, 0); memset(&out, 0xa5, sizeof(out));
        CHECK(snd_channel_get_status_ex(3, &out) == -1 && errno == EAGAIN);
        CHECK(attempts == 8 && locks == 8 && yields == 7 && !memcmp(&out, &zero, sizeof(out))); restored();
    }
    reset(4, 0, 0);
    CHECK(snd_channel_get_status_ex(3, &out) == 0 && attempts == 2 && yields == 1);
    CHECK(out.sequence == 4 && out.position == 40); restored();
    reset(0, 0, 0); initted = 0; memset(&out, 0xa5, sizeof(out));
    CHECK(snd_channel_get_status_ex(3, &out) == -1 && errno == ENODEV && !locks && !yields);
    CHECK(!memcmp(&out, &zero, sizeof(out)));
    reset(0, 0, 0); driver_features = 0;
    CHECK(snd_channel_get_status_ex(3, &out) == -1 && errno == ENOTSUP && !locks && !yields);
    reset(0, 0, 0);
    CHECK(snd_channel_get_status_ex(64, &out) == -1 && errno == EINVAL && !locks && !yields);
    CHECK(snd_channel_get_status_ex(3, NULL) == -1 && errno == EINVAL && !locks && !yields);
    printf("Channel status cooperative retry: %u checks PASS; exact context/cleanup/bounds\n", checks);
#endif
    return 0;
}
