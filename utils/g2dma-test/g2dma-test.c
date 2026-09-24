/* KallistiOS ##version##

   g2dma-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/g2bus.h>
#include <dc/asic.h>
#include <arch/arch.h>
#include <kos/sem.h>

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DMA_SIZE_MASK UINT32_C(0x7fffffff)
#define RESET_ENABLED UINT32_C(0x80000000)
#define PROTECTION_DISABLED UINT32_C(0x4659007f)
#define PROTECTION_ENABLED  UINT32_C(0x46597f00)

typedef struct test_dma_channel {
    uint32_t g2_addr;
    uint32_t sh4_addr;
    uint32_t size;
    uint32_t dir;
    uint32_t trigger_select;
    uint32_t enable;
    uint32_t start;
    uint32_t suspend;
} test_dma_channel_t;

typedef struct test_dma_regs {
    test_dma_channel_t dma[4];
    uint32_t g2_id;
    uint32_t unused1[3];
    uint32_t ds_timeout;
    uint32_t tr_timeout;
    uint32_t modem_timeout;
    uint32_t modem_wait;
    uint32_t unused2[7];
    uint32_t protection;
} test_dma_regs_t;

uint32_t g2_test_dma_registers[sizeof(test_dma_regs_t) / sizeof(uint32_t)];
uint32_t g2_test_fifo_status;
unsigned int g2_test_mem_size = HW_MEM_16;

static uint32_t suspend_state[4];
static uint32_t irq_depth;
static bool inside_irq;
static bool test_mmu_enabled;
static unsigned int failures;
static unsigned int cache_wbacks;
static unsigned int cache_invalidations;
static uintptr_t last_cache_address;
static size_t last_cache_length;
static unsigned int schedules;
static unsigned int semaphore_inits;
static unsigned int semaphore_destroys;
static void (*semaphore_wait_hook)(void);
static asic_evt_handler claim_handlers[4];
static void *claim_data[4];
static asic_evt_claim_t claim_tokens[4];
static uint16_t claim_generation[4];
static int claim_failure_channel = -1;
static unsigned int claim_count;
static unsigned int release_count;
static bool stop_stuck;
static uint64_t test_time;
static unsigned int checks;
static unsigned int acknowledgements;
static int sem_failure_after = -1;

#define CHECK(condition) do { \
    ++checks; \
    if(!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

static test_dma_regs_t *registers(void) {
    return (test_dma_regs_t *)g2_test_dma_registers;
}

uint64_t timer_ms_gettime64(void) { return test_time; }
uint32_t g2_test_start_read(uint32_t channel) {
    return registers()->dma[channel].start;
}
void g2_test_start_write(uint32_t channel, uint32_t value) {
    if(value || !stop_stuck) registers()->dma[channel].start = value;
}
void g2_test_ack(uint32_t channel) {
    CHECK(channel < 4);
    ++acknowledgements;
}

uint32_t g2_test_suspend_read(uint32_t channel) {
    CHECK(channel < 4);
    return suspend_state[channel];
}

void g2_test_suspend_write(uint32_t channel, uint32_t value) {
    CHECK(channel < 4);
    suspend_state[channel] = value;
}

irq_mask_t irq_disable(void) {
    irq_mask_t previous = irq_depth;
    ++irq_depth;
    return previous;
}

void irq_restore(irq_mask_t state) {
    irq_depth = state;
}

bool irq_inside_int(void) {
    return inside_irq;
}

bool mmu_enabled(void) {
    return test_mmu_enabled;
}

int hardware_sys_mode(int *region) {
    if(region)
        *region = 0;
    return HW_TYPE_RETAIL;
}

void dcache_wback_range(uintptr_t address, size_t length) {
    ++cache_wbacks;
    last_cache_address = address;
    last_cache_length = length;
}

void dcache_inval_range(uintptr_t address, size_t length) {
    ++cache_invalidations;
    last_cache_address = address;
    last_cache_length = length;
}

void dbglog(int level, const char *format, ...) {
    (void)level;
    (void)format;
}

void thd_schedule(bool front_of_line) {
    CHECK(front_of_line);
    ++schedules;
}

int sem_init(semaphore_t *semaphore, int count) {
    if(sem_failure_after == 0) { errno = ENOMEM; return -1; }
    if(sem_failure_after > 0) --sem_failure_after;
    semaphore->count = count;
    semaphore->initialized = 1;
    ++semaphore_inits;
    return 0;
}

int sem_destroy(semaphore_t *semaphore) {
    semaphore->count = 0;
    semaphore->initialized = 0;
    ++semaphore_destroys;
    return 0;
}

int sem_wait_timed(semaphore_t *semaphore, unsigned int timeout) {
    CHECK(semaphore->initialized);

    if(semaphore->count <= 0 && semaphore_wait_hook) {
        void (*hook)(void) = semaphore_wait_hook;

        semaphore_wait_hook = NULL;
        hook();
    }

    if(semaphore->count > 0) {
        --semaphore->count;
        return 0;
    }

    test_time += timeout ? timeout : 1;
    errno = ETIMEDOUT;
    return -1;
}

int sem_trywait(semaphore_t *semaphore) {
    CHECK(semaphore->initialized);
    if(semaphore->count <= 0) {
        errno = EWOULDBLOCK;
        return -1;
    }

    --semaphore->count;
    return 0;
}

int sem_signal(semaphore_t *semaphore) {
    CHECK(semaphore->initialized);
    ++semaphore->count;
    return 0;
}

int sem_count(const semaphore_t *semaphore) {
    return semaphore->count;
}

static int claim_index(uint16_t code) {
    return (int)code - (int)ASIC_EVT_G2_DMA0;
}

int asic_evt_claim(uint16_t code, uint8_t irqlevel,
                   asic_evt_handler handler, void *data,
                   asic_evt_claim_t *claim) {
    int index = claim_index(code);

    CHECK(index >= 0 && index < 4);
    CHECK(irqlevel == ASIC_IRQB);

    if(index == claim_failure_channel || claim_tokens[index]) {
        *claim = ASIC_EVT_CLAIM_INVALID;
        errno = EBUSY;
        return -1;
    }

    ++claim_generation[index];
    if(!claim_generation[index])
        ++claim_generation[index];
    claim_tokens[index] = ((uint32_t)claim_generation[index] << 16) | code;
    claim_handlers[index] = handler;
    claim_data[index] = data;
    *claim = claim_tokens[index];
    ++claim_count;
    return 0;
}

int asic_evt_release(asic_evt_claim_t claim) {
    int index = claim_index((uint16_t)claim);

    if(index < 0 || index >= 4 || claim_tokens[index] != claim) {
        errno = ENOENT;
        return -1;
    }

    claim_tokens[index] = ASIC_EVT_CLAIM_INVALID;
    claim_handlers[index] = NULL;
    claim_data[index] = NULL;
    ++release_count;
    return 0;
}

static void invoke_completion(uint32_t channel) {
    bool previous = inside_irq;

    CHECK(channel < 4 && claim_handlers[channel] != NULL);
    registers()->dma[channel].size = 0;
    registers()->dma[channel].start = 0;
    inside_irq = true;
    claim_handlers[channel](ASIC_EVT_G2_DMA0 + channel, claim_data[channel]);
    inside_irq = previous;
}

static void test_lock_preserves_state(void) {
    static const uint32_t initial[4] = { 0, 1, 0, 1 };
    g2_ctx_t outer;
    g2_ctx_t inner;
    uint32_t channel;

    memcpy(suspend_state, initial, sizeof(initial));
    outer = g2_lock();
    CHECK(irq_depth == 1);
    for(channel = 0; channel < 4; ++channel)
        CHECK(suspend_state[channel] == 1);
    g2_unlock(outer);
    CHECK(irq_depth == 0);
    CHECK(memcmp(suspend_state, initial, sizeof(initial)) == 0);

    memset(suspend_state, 0, sizeof(suspend_state));
    outer = g2_lock();
    inner = g2_lock();
    g2_unlock(inner);
    for(channel = 0; channel < 4; ++channel)
        CHECK(suspend_state[channel] == 1);
    g2_unlock(outer);
    for(channel = 0; channel < 4; ++channel)
        CHECK(suspend_state[channel] == 0);
}

static unsigned int callback_count;

static void count_callback(void *data) {
    unsigned int *count = data;
    ++*count;
}

static void test_validation_and_status(void) {
    g2_dma_status_t status;
    g2_dma_status_t zero = { 0 };
    unsigned int wbacks_before;
    unsigned int invalidations_before;

    memset(&status, 0xaa, sizeof(status));
    errno = 0;
    CHECK(g2_dma_get_status(4, &status) < 0 && errno == EINVAL);
    CHECK(memcmp(&status, &zero, sizeof(status)) == 0);

    CHECK(g2_dma_get_status(G2_DMA_CHAN_SPU, &status) == 0);
    CHECK(status.state == G2_DMA_STATE_IDLE && status.sequence == 0);

    errno = 0;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000000),
                          (void *)(uintptr_t)UINT32_C(0x00800000), 0, 0, NULL, NULL,
                          G2_DMA_TO_G2, 0, 0, 0) < 0 && errno == EINVAL);
    errno = 0;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000000),
                          (void *)(uintptr_t)UINT32_C(0x00800000), 33, 0, NULL, NULL,
                          G2_DMA_TO_G2, 0, 0, 0) < 0 && errno == EINVAL);
    errno = 0;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000000),
                          (void *)(uintptr_t)UINT32_C(0x00800000), 32, 0, NULL, NULL,
                          2, 0, 0, 0) < 0 && errno == EINVAL);

    test_mmu_enabled = true;
    errno = 0;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x0c000000),
                          (void *)(uintptr_t)UINT32_C(0x00800000), 32, 0, NULL, NULL,
                          G2_DMA_TO_G2, 0, 0, 0) < 0 && errno == EFAULT);
    errno = 0;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000001),
                          (void *)(uintptr_t)UINT32_C(0x00800000), 32, 0, NULL, NULL,
                          G2_DMA_TO_G2, 0, 0, 0) < 0 && errno == EFAULT);

    inside_irq = true;
    errno = 0;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000000),
                          (void *)(uintptr_t)UINT32_C(0x00800000), 32, 1, NULL, NULL,
                          G2_DMA_TO_G2, 0, 0, 0) < 0 && errno == EPERM);
    inside_irq = false;

    /* PVR RAM is a root-bus endpoint, but unlike system RAM it never receives
       SH-4 data-cache maintenance. Both hardware apertures remain distinct. */
    wbacks_before = cache_wbacks;
    invalidations_before = cache_invalidations;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0xa4000000),
                          (void *)(uintptr_t)UINT32_C(0x00800000), 32, 0,
                          NULL, NULL, G2_DMA_TO_SH4, 0,
                          G2_DMA_CHAN_SPU, 0) == 0);
    CHECK(g2_dma_get_status(G2_DMA_CHAN_SPU, &status) == 0);
    CHECK(status.root_region == G2_DMA_ROOT_VRAM_64);
    CHECK(cache_wbacks == wbacks_before);
    CHECK(cache_invalidations == invalidations_before);
    CHECK(g2_dma_cancel(G2_DMA_CHAN_SPU) == 0);

    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0xa5000000),
                          (void *)(uintptr_t)UINT32_C(0x00800000), 32, 0,
                          NULL, NULL, G2_DMA_TO_G2, 0,
                          G2_DMA_CHAN_SPU, 0) == 0);
    CHECK(g2_dma_get_status(G2_DMA_CHAN_SPU, &status) == 0);
    CHECK(status.root_region == G2_DMA_ROOT_VRAM_32);
    CHECK(cache_wbacks == wbacks_before);
    CHECK(cache_invalidations == invalidations_before);
    CHECK(g2_dma_cancel(G2_DMA_CHAN_SPU) == 0);

    errno = 0;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0xa47fffe0),
                          (void *)(uintptr_t)UINT32_C(0x00800000), 64, 0,
                          NULL, NULL, G2_DMA_TO_G2, 0,
                          G2_DMA_CHAN_SPU, 0) < 0 && errno == EFAULT);


}

static void test_transfer_completion(void) {
    g2_dma_status_t status;
    test_dma_channel_t *channel = &registers()->dma[G2_DMA_CHAN_BBA];

    test_mmu_enabled = true;
    cache_wbacks = 0;
    callback_count = 0;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000000),
                          (void *)(uintptr_t)UINT32_C(0x00800000), 64, 0,
                          count_callback, &callback_count, G2_DMA_TO_G2,
                          0, G2_DMA_CHAN_BBA, 0) == 0);
    CHECK(cache_wbacks == 1);
    CHECK(last_cache_address == (uintptr_t)UINT32_C(0x8c000000));
    CHECK(last_cache_length == 64);
    CHECK(channel->g2_addr == UINT32_C(0x00800000));
    CHECK(channel->sh4_addr == UINT32_C(0x0c000000));
    CHECK(channel->size == (RESET_ENABLED | UINT32_C(64)));
    CHECK(channel->enable == 1 && channel->start == 1);

    CHECK(g2_dma_get_status(G2_DMA_CHAN_BBA, &status) == 0);
    CHECK(status.state == G2_DMA_STATE_RUNNING);
    CHECK(status.requested_bytes == 64 && status.remaining_bytes == 64);
    CHECK(status.callback_pending);

    invoke_completion(G2_DMA_CHAN_BBA);
    CHECK(callback_count == 1);
    CHECK(g2_dma_get_status(G2_DMA_CHAN_BBA, &status) == 0);
    CHECK(status.state == G2_DMA_STATE_COMPLETE);
    CHECK(status.completions == 1 && status.remaining_bytes == 0);
    CHECK(!status.callback_pending && status.result == 0);
}

static void test_retail_32mb_ranges(void) {
    static const uintptr_t addresses[] = {
        0x8d000000u, 0xad000000u, 0x8dffffe0u, 0xadffffe0u
    };
    g2_dma_status_t status;
    unsigned int before;
    size_t i;

    /* A RAM-modded retail board still has retail VRAM and sound RAM. */
    CHECK(hardware_sys_mode(NULL) == HW_TYPE_RETAIL);
    for(i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        g2_test_mem_size = HW_MEM_16;
        errno = 0;
        CHECK(g2_dma_transfer((void *)addresses[i], (void *)0x00800000u,
                              32, 0, NULL, NULL, G2_DMA_TO_G2, 0,
                              G2_DMA_CHAN_SPU, 0) < 0 && errno == EFAULT);

        g2_test_mem_size = HW_MEM_32;
        before = cache_wbacks;
        CHECK(g2_dma_transfer((void *)addresses[i], (void *)0x00800000u,
                              32, 0, NULL, NULL, G2_DMA_TO_G2, 0,
                              G2_DMA_CHAN_SPU, 0) == 0);
        CHECK(g2_dma_get_status(G2_DMA_CHAN_SPU, &status) == 0);
        CHECK(status.root_region == G2_DMA_ROOT_SYSTEM_RAM);
        CHECK(registers()->dma[G2_DMA_CHAN_SPU].sh4_addr ==
              (addresses[i] & 0x1fffffffu));
        CHECK(cache_wbacks == before + (addresses[i] < 0xa0000000u ? 1u : 0u));
        CHECK(g2_dma_cancel(G2_DMA_CHAN_SPU) == 0);
    }

    /* Upper bank readback and a transfer crossing the old 16 MiB boundary. */
    CHECK(g2_dma_transfer((void *)0x8d000000u, (void *)0x00800000u,
                          32, 0, NULL, NULL, G2_DMA_TO_SH4, 0,
                          G2_DMA_CHAN_SPU, 0) == 0);
    CHECK(g2_dma_cancel(G2_DMA_CHAN_SPU) == 0);
    CHECK(g2_dma_transfer((void *)0x8cffffe0u, (void *)0x00800000u,
                          64, 0, NULL, NULL, G2_DMA_TO_G2, 0,
                          G2_DMA_CHAN_SPU, 0) == 0);
    CHECK(g2_dma_cancel(G2_DMA_CHAN_SPU) == 0);

    errno = 0;
    CHECK(g2_dma_transfer((void *)0x8dffffe0u, (void *)0x00800000u,
                          64, 0, NULL, NULL, G2_DMA_TO_G2, 0,
                          G2_DMA_CHAN_SPU, 0) < 0 && errno == EFAULT);
    errno = 0;
    CHECK(g2_dma_transfer((void *)0xa4800000u, (void *)0x00800000u,
                          32, 0, NULL, NULL, G2_DMA_TO_G2, 0,
                          G2_DMA_CHAN_SPU, 0) < 0 && errno == EFAULT);
    g2_test_mem_size = HW_MEM_16;
}

static void test_suspend_cancel_and_wait(void) {
    g2_dma_status_t status;
    unsigned int callback_before = callback_count;

    cache_invalidations = 0;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000040),
                          (void *)(uintptr_t)UINT32_C(0x00800040), 32, 0,
                          count_callback, &callback_count, G2_DMA_TO_SH4,
                          0, G2_DMA_CHAN_CH2, 0) == 0);
    CHECK(cache_invalidations == 1);
    CHECK(g2_dma_suspend(G2_DMA_CHAN_CH2) == 0);
    CHECK(registers()->dma[G2_DMA_CHAN_CH2].suspend == 1);
    CHECK(g2_dma_resume(G2_DMA_CHAN_CH2) == 0);
    CHECK(registers()->dma[G2_DMA_CHAN_CH2].suspend == 0);
    CHECK(g2_dma_cancel(G2_DMA_CHAN_CH2) == 0);
    CHECK(cache_invalidations == 2);
    CHECK(callback_count == callback_before);
    CHECK(registers()->dma[G2_DMA_CHAN_CH2].enable == 0);
    CHECK(registers()->dma[G2_DMA_CHAN_CH2].start == 0);

    CHECK(g2_dma_get_status(G2_DMA_CHAN_CH2, &status) == 0);
    CHECK(status.state == G2_DMA_STATE_CANCELLED);
    CHECK(status.result == ECANCELED && status.cancellations == 1);
    CHECK(!status.callback_pending);
    errno = 0;
    CHECK(g2_dma_wait(G2_DMA_CHAN_CH2, 1) < 0 && errno == ECANCELED);

    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000080),
                          (void *)(uintptr_t)UINT32_C(0x00800080), 32, 0,
                          NULL, NULL, G2_DMA_TO_G2, 0,
                          G2_DMA_CHAN_CH3, 0) == 0);
    errno = 0;
    CHECK(g2_dma_wait(G2_DMA_CHAN_CH3, 1) < 0 && errno == ETIMEDOUT);
    CHECK(g2_dma_get_status(G2_DMA_CHAN_CH3, &status) == 0);
    CHECK(status.state == G2_DMA_STATE_RUNNING);
    CHECK(g2_dma_cancel(G2_DMA_CHAN_CH3) == 0);
}

static void complete_channel_zero(void) {
    invoke_completion(G2_DMA_CHAN_SPU);
}

static void test_blocking_transfer(void) {
    g2_dma_status_t status;

    semaphore_wait_hook = complete_channel_zero;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c0000a0),
                          (void *)(uintptr_t)UINT32_C(0x008000a0), 32, 1,
                          NULL, NULL, G2_DMA_TO_G2, 0,
                          G2_DMA_CHAN_SPU, 0) == 0);
    CHECK(g2_dma_get_status(G2_DMA_CHAN_SPU, &status) == 0);
    CHECK(status.state == G2_DMA_STATE_COMPLETE);
    CHECK(status.completions == 1);
    CHECK(schedules == 1);
}

static unsigned int chained_callbacks;

static void second_callback(void *data) {
    (void)data;
    ++chained_callbacks;
}

static void first_callback(void *data) {
    (void)data;
    ++chained_callbacks;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000100),
                          (void *)(uintptr_t)UINT32_C(0x00800100), 32, 0,
                          second_callback, NULL, G2_DMA_TO_G2, 0,
                          G2_DMA_CHAN_BBA, 0) == 0);
}

static void test_callback_chaining(void) {
    g2_dma_status_t status;
    uint64_t first_sequence;

    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c0000e0),
                          (void *)(uintptr_t)UINT32_C(0x008000e0), 32, 0,
                          first_callback, NULL, G2_DMA_TO_G2, 0,
                          G2_DMA_CHAN_BBA, 0) == 0);
    CHECK(g2_dma_get_status(G2_DMA_CHAN_BBA, &status) == 0);
    first_sequence = status.sequence;
    invoke_completion(G2_DMA_CHAN_BBA);
    CHECK(chained_callbacks == 1);
    CHECK(g2_dma_get_status(G2_DMA_CHAN_BBA, &status) == 0);
    CHECK(status.state == G2_DMA_STATE_RUNNING);
    CHECK(status.sequence == first_sequence + 1);
    CHECK(status.callback_pending);

    invoke_completion(G2_DMA_CHAN_BBA);
    CHECK(chained_callbacks == 2);
    CHECK(g2_dma_get_status(G2_DMA_CHAN_BBA, &status) == 0);
    CHECK(status.state == G2_DMA_STATE_COMPLETE);
    CHECK(status.completions == 3);
    CHECK(!status.callback_pending);
}

static int submit_zero(uint32_t block, g2_dma_callback_t callback) {
    return g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000200),
                          (void *)(uintptr_t)UINT32_C(0x00800200), 32,
                          block, callback, NULL, G2_DMA_TO_G2, 0, 0, 0);
}

static void chain_cancelled(void *data) {
    (void)data;
    /* A second blocking operation cannot take the old waiter's slot, but an
       asynchronous callback chain is still supported. */
    inside_irq = false; /* model another thread before the waiter resumes */
    CHECK(submit_zero(1, NULL) < 0 && errno == EBUSY);
    inside_irq = true;
    CHECK(submit_zero(0, NULL) == 0);
    CHECK(g2_dma_cancel(0) == 0);
}

static void cancel_then_replace(void) {
    CHECK(g2_dma_wait(0, 1) < 0 && errno == EBUSY);
    CHECK(g2_dma_cancel(0) == 0);
    CHECK(submit_zero(0, NULL) == 0);
    invoke_completion(0);
}

static void shutdown_while_waiting(void) {
    unsigned int destroyed = semaphore_destroys, released = release_count;
    errno = 0;
    g2_dma_shutdown();
    CHECK(errno == EBUSY);
    CHECK(g2_dma_init() == 0);
    invoke_completion(0);
    errno = 0;
    g2_dma_shutdown();
    CHECK(errno == EBUSY); /* signaled is not the same as consumed */
    CHECK(semaphore_destroys == destroyed && release_count == released);
}

static void test_waiter_generations(void) {
    g2_dma_status_t status;
    semaphore_wait_hook = complete_channel_zero;
    CHECK(submit_zero(1, chain_cancelled) == 0);
    CHECK(g2_dma_get_status(0, &status) == 0);
    CHECK(status.state == G2_DMA_STATE_CANCELLED); /* later generation */

    CHECK(submit_zero(0, NULL) == 0);
    semaphore_wait_hook = cancel_then_replace;
    CHECK(g2_dma_wait(0, 10) < 0 && errno == ECANCELED);
    CHECK(g2_dma_get_status(0, &status) == 0);
    CHECK(status.state == G2_DMA_STATE_COMPLETE); /* old cancellation retained */

    semaphore_wait_hook = shutdown_while_waiting;
    CHECK(submit_zero(1, NULL) == 0);
    CHECK(submit_zero(0, NULL) == 0);
    CHECK(g2_dma_wait(0, 7) < 0 && errno == ETIMEDOUT);
    /* Timed-out wait role is released; another waiter may attach. */
    semaphore_wait_hook = complete_channel_zero;
    CHECK(g2_dma_wait(0, 10) == 0);
}

static void test_failed_stop_retains_ownership(void) {
    g2_dma_status_t before, after;
    unsigned int invalidated = cache_invalidations;
    unsigned int calls = callback_count, destroyed = semaphore_destroys;
    unsigned int irq_releases = release_count;
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000200),
                          (void *)(uintptr_t)UINT32_C(0x00800000), 32, 0,
                          count_callback, &callback_count, G2_DMA_TO_SH4,
                          0, 1, 0) == 0);
    CHECK(g2_dma_get_status(1, &before) == 0);
    stop_stuck = true;
    CHECK(g2_dma_cancel(1) < 0 && errno == EBUSY);
    CHECK(callback_count == calls);
    CHECK(cache_invalidations == invalidated + 1);
    errno = 0;
    g2_dma_shutdown();
    CHECK(errno == EBUSY);
    CHECK(semaphore_destroys == destroyed && release_count == irq_releases);
    CHECK(g2_dma_init() == 0);
    CHECK(g2_dma_get_status(1, &after) == 0);
    CHECK(after.sequence == before.sequence && after.callback_pending);
    CHECK(after.state == G2_DMA_STATE_RUNNING && after.result == EINPROGRESS);
    /* A delayed old event must not publish success for a still-busy engine. */
    inside_irq = true;
    claim_handlers[1](ASIC_EVT_G2_DMA1, claim_data[1]);
    inside_irq = false;
    CHECK(callback_count == calls);
    stop_stuck = false;
    invoke_completion(1);
    CHECK(callback_count == calls + 1);
    CHECK(cache_invalidations == invalidated + 2);

    CHECK(submit_zero(0, NULL) == 0);
    CHECK(g2_dma_suspend(0) == 0);
    CHECK(g2_dma_cancel(0) == 0);
    CHECK(submit_zero(0, NULL) == 0);
    CHECK(registers()->dma[0].suspend == 0);
    CHECK(g2_dma_cancel(0) == 0);

    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000200),
                          (void *)(uintptr_t)UINT32_C(0x04000000), 32, 0,
                          NULL, NULL, G2_DMA_TO_G2, 0, 0, 0) < 0
          && errno == EFAULT);
    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000200),
                          (void *)(uintptr_t)UINT32_C(0x03ffffe0), 64, 0,
                          NULL, NULL, G2_DMA_TO_G2, 0, 0, 0) < 0
          && errno == EFAULT);
}

static void test_init_failure_and_shutdown(void) {
    unsigned int releases_before;
    unsigned int destroys_before;
    unsigned int callback_before = callback_count;
    uint32_t channel;

    CHECK(g2_dma_transfer((void *)(uintptr_t)UINT32_C(0x8c000120),
                          (void *)(uintptr_t)UINT32_C(0x00800120), 32, 0,
                          count_callback, &callback_count, G2_DMA_TO_G2, 0,
                          G2_DMA_CHAN_CH2, 0) == 0);
    g2_dma_shutdown();
    CHECK(callback_count == callback_before);
    CHECK(registers()->protection == PROTECTION_ENABLED);
    for(channel = 0; channel < 4; ++channel)
        CHECK(claim_tokens[channel] == ASIC_EVT_CLAIM_INVALID);

    releases_before = release_count;
    destroys_before = semaphore_destroys;
    claim_failure_channel = G2_DMA_CHAN_CH2;
    errno = 0;
    CHECK(g2_dma_init() < 0 && errno == EBUSY);
    CHECK(release_count == releases_before + 2);
    CHECK(semaphore_destroys == destroys_before + 3);
    for(channel = 0; channel < 4; ++channel)
        CHECK(claim_tokens[channel] == ASIC_EVT_CLAIM_INVALID);

    claim_failure_channel = -1;
    CHECK(g2_dma_init() == 0);
    CHECK(registers()->ds_timeout == 27);
    CHECK(registers()->protection == PROTECTION_DISABLED);

    inside_irq = true;
    errno = 0;
    g2_dma_shutdown();
    CHECK(errno == EPERM);
    inside_irq = false;
    CHECK(g2_dma_get_status(G2_DMA_CHAN_SPU, &(g2_dma_status_t){ 0 }) == 0);

    g2_dma_shutdown();

    for(int failure = 0; failure < 4; ++failure) {
        unsigned int destroyed = semaphore_destroys;
        unsigned int released = release_count;
        sem_failure_after = failure;
        CHECK(g2_dma_init() < 0 && errno == ENOMEM);
        CHECK(semaphore_destroys == destroyed + (unsigned int)failure);
        CHECK(release_count == released + (unsigned int)failure);
    }
    sem_failure_after = -1;
    registers()->dma[0].start = 1;
    CHECK(g2_dma_init() < 0 && errno == EBUSY);
    registers()->dma[0].start = 0;
    CHECK(g2_dma_init() == 0);
    g2_dma_shutdown();

    inside_irq = true;
    errno = 0;
    CHECK(g2_dma_init() < 0 && errno == EPERM);
    inside_irq = false;
}

int main(void) {
    memset(g2_test_dma_registers, 0, sizeof(g2_test_dma_registers));
    test_lock_preserves_state();

    CHECK(g2_dma_init() == 0);
    CHECK(claim_count == 4);
    CHECK(registers()->ds_timeout == 27);
    CHECK(registers()->protection == PROTECTION_DISABLED);

    test_validation_and_status();
    test_transfer_completion();
    test_retail_32mb_ranges();
    test_suspend_cancel_and_wait();
    test_blocking_transfer();
    test_callback_chaining();
    test_waiter_generations();
    test_failed_stop_retains_ownership();
    test_init_failure_and_shutdown();

    CHECK(irq_depth == 0);
    CHECK(semaphore_inits == semaphore_destroys);

    if(failures) {
        fprintf(stderr, "%u G2 DMA checks failed\n", failures);
        return EXIT_FAILURE;
    }

    printf("G2 DMA tests passed: checks=%u\n", checks);
    return EXIT_SUCCESS;
}
