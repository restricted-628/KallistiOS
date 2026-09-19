/* KallistiOS ##version##

   asic-event-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/asic.h>
#include <kos/irq.h>
#include <kos/worker_thread.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MMIO_BASE ASIC_ACK_A
#define MMIO_END  (ASIC_IRQ9_C + sizeof(uint32_t))
#define MMIO_WORDS ((MMIO_END - MMIO_BASE) / sizeof(uint32_t))

struct kthread_worker {
    void (*routine)(void *);
    void *data;
    kthread_t thread;
};

typedef struct irq_slot {
    irq_hdl_t handler;
    void *data;
} irq_slot_t;

static uint32_t mmio[MMIO_WORDS];
static irq_slot_t irq_slots[16];
static kthread_t *current_thread;
static unsigned int worker_creates;
static unsigned int worker_destroys;
static unsigned int failures;

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

static size_t mmio_index(uintptr_t address) {
    CHECK(address >= MMIO_BASE && address < MMIO_END);
    CHECK((address & 3u) == 0);
    return (address - MMIO_BASE) / sizeof(uint32_t);
}

uint32_t asic_test_read32(uintptr_t address) {
    return mmio[mmio_index(address)];
}

void asic_test_write32(uintptr_t address, uint32_t value) {
    size_t index = mmio_index(address);

    /* The three status registers acknowledge pending events by writing ones. */
    if(address >= ASIC_ACK_A && address <= ASIC_ACK_C)
        mmio[index] &= ~value;
    else
        mmio[index] = value;
}

irq_mask_t irq_disable(void) {
    return 0;
}

void irq_restore(irq_mask_t state) {
    (void)state;
}

void irq_set_handler(irq_t code, irq_hdl_t handler, void *data) {
    CHECK(code >= 0 && (size_t)code < sizeof(irq_slots) / sizeof(irq_slots[0]));
    irq_slots[code].handler = handler;
    irq_slots[code].data = data;
}

kthread_worker_t *thd_worker_create(void (*routine)(void *), void *data) {
    kthread_worker_t *worker = calloc(1, sizeof(*worker));

    if(worker) {
        worker->routine = routine;
        worker->data = data;
        ++worker_creates;
    }

    return worker;
}

void thd_worker_destroy(kthread_worker_t *worker) {
    CHECK(worker != NULL);
    ++worker_destroys;
    free(worker);
}

void thd_worker_wakeup(kthread_worker_t *worker) {
    kthread_t *previous = current_thread;

    current_thread = &worker->thread;
    worker->routine(worker->data);
    current_thread = previous;
}

kthread_t *thd_worker_get_thread(kthread_worker_t *worker) {
    return &worker->thread;
}

kthread_t *thd_get_current(void) {
    return current_thread;
}

static uintptr_t mask_address(uint8_t level, uint16_t code) {
    return ASIC_IRQD_A + level * UINT32_C(0x10)
        + ((code >> 8) & UINT32_C(0xff)) * sizeof(uint32_t);
}

static void set_pending(uint16_t code) {
    uintptr_t address = ASIC_ACK_A
        + ((code >> 8) & UINT32_C(0xff)) * sizeof(uint32_t);

    mmio[mmio_index(address)] |= UINT32_C(1) << (code & UINT16_C(0xff));
}

static void dispatch(uint8_t level) {
    static const irq_t sources[ASIC_IRQ_MAX] = { EXC_IRQ9, EXC_IRQB, EXC_IRQD };
    irq_slot_t *slot = &irq_slots[sources[level]];

    CHECK(slot->handler != NULL);
    slot->handler(sources[level], NULL, slot->data);
}

static void count_handler(uint32_t code, void *data) {
    unsigned int *count = data;

    CHECK(code == ASIC_EVT_GD_DMA_ILLADDR);
    ++*count;
}

static void other_handler(uint32_t code, void *data) {
    (void)code;
    (void)data;
}

static void test_claim_lifecycle(void) {
    const uint16_t code = ASIC_EVT_GD_DMA_ILLADDR;
    asic_evt_status_t status;
    asic_evt_claim_t first = UINT32_MAX;
    asic_evt_claim_t second = ASIC_EVT_CLAIM_INVALID;
    unsigned int count = 0;

    CHECK(asic_evt_claim(code, ASIC_IRQB, count_handler, &count, &first) == 0);
    CHECK(first != ASIC_EVT_CLAIM_INVALID);
    CHECK(asic_test_read32(mask_address(ASIC_IRQB, code))
          & (UINT32_C(1) << (code & UINT16_C(0xff))));

    CHECK(asic_evt_get_status(code, &status) == 0);
    CHECK(status.code == code);
    CHECK(status.enabled_levels == (UINT8_C(1) << ASIC_IRQB));
    CHECK(status.handler_present);
    CHECK(status.exclusively_claimed);
    CHECK(status.dispatches == 0);

    set_pending(code);
    dispatch(ASIC_IRQB);
    CHECK(count == 1);
    CHECK(asic_evt_get_status(code, &status) == 0);
    CHECK(status.dispatches == 1);

    CHECK(asic_evt_claim_mask(first) == 0);
    CHECK(!(asic_test_read32(mask_address(ASIC_IRQB, code))
            & (UINT32_C(1) << (code & UINT16_C(0xff)))));
    CHECK(asic_evt_claim_unmask(first) == 0);

    errno = 0;
    CHECK(asic_evt_set_handler(code, other_handler, NULL).hdl == count_handler);
    CHECK(errno == EBUSY);
    errno = 0;
    asic_evt_disable(code, ASIC_IRQB);
    CHECK(errno == EBUSY);
    errno = 0;
    asic_evt_remove_handler(code);
    CHECK(errno == EBUSY);

    CHECK(asic_evt_release(first) == 0);
    CHECK(asic_evt_claim(code, ASIC_IRQD, count_handler, &count, &second) == 0);
    CHECK(second != first);
    errno = 0;
    CHECK(asic_evt_claim_mask(first) < 0 && errno == ENOENT);
    CHECK(asic_evt_release(second) == 0);
}

static void test_admission_and_errors(void) {
    const uint16_t code = ASIC_EVT_GD_DMA_OVERRUN;
    asic_evt_status_t status = { .dispatches = UINT64_MAX };
    asic_evt_claim_t claim = UINT32_MAX;

    asic_test_write32(mask_address(ASIC_IRQ9, code),
                      UINT32_C(1) << (code & UINT16_C(0xff)));
    errno = 0;
    CHECK(asic_evt_claim(code, ASIC_IRQB, other_handler, NULL, &claim) < 0);
    CHECK(errno == EBUSY);
    CHECK(claim == ASIC_EVT_CLAIM_INVALID);
    CHECK(asic_test_read32(mask_address(ASIC_IRQ9, code)) != 0);
    asic_test_write32(mask_address(ASIC_IRQ9, code), 0);

    asic_evt_set_handler(code, other_handler, NULL);
    errno = 0;
    CHECK(asic_evt_claim(code, ASIC_IRQB, other_handler, NULL, &claim) < 0);
    CHECK(errno == EBUSY && claim == ASIC_EVT_CLAIM_INVALID);
    asic_evt_remove_handler(code);

    errno = 0;
    CHECK(asic_evt_claim(UINT16_C(0x0300), ASIC_IRQ9, other_handler,
                         NULL, &claim) < 0);
    CHECK(errno == EINVAL && claim == ASIC_EVT_CLAIM_INVALID);
    errno = 0;
    CHECK(asic_evt_claim(code, ASIC_IRQ_MAX, other_handler,
                         NULL, &claim) < 0);
    CHECK(errno == EINVAL && claim == ASIC_EVT_CLAIM_INVALID);
    errno = 0;
    CHECK(asic_evt_claim(code, ASIC_IRQ9, NULL, NULL, &claim) < 0);
    CHECK(errno == EINVAL && claim == ASIC_EVT_CLAIM_INVALID);
    errno = 0;
    CHECK(asic_evt_claim(code, ASIC_IRQ9, other_handler, NULL, NULL) < 0);
    CHECK(errno == EINVAL);

    errno = 0;
    CHECK(asic_evt_get_status(UINT16_C(0x0300), &status) < 0);
    CHECK(errno == EINVAL && status.dispatches == 0);
    errno = 0;
    CHECK(asic_evt_get_status(code, NULL) < 0 && errno == EINVAL);
}

static uint16_t threaded_code;
static unsigned int threaded_calls;
static unsigned int mask_calls;
static unsigned int unmask_calls;

static void threaded_mask(uint16_t code) {
    CHECK(code == threaded_code);
    ++mask_calls;
}

static void threaded_unmask(uint16_t code) {
    CHECK(code == threaded_code);
    ++unmask_calls;
}

static void threaded_handler(uint32_t code, void *data) {
    (void)data;
    CHECK(code == threaded_code);
    ++threaded_calls;

    errno = 0;
    asic_evt_remove_handler((uint16_t)code);
    CHECK(errno == EDEADLK);
}

static void test_threaded_lifecycle(void) {
    unsigned int destroys_before = worker_destroys;

    threaded_code = ASIC_EVT_EXP_8BIT;
    CHECK(asic_evt_request_threaded_handler(threaded_code, threaded_handler,
                                            NULL, threaded_mask,
                                            threaded_unmask) == 0);
    errno = 0;
    CHECK(asic_evt_request_threaded_handler(threaded_code, other_handler,
                                            NULL, NULL, NULL) < 0);
    CHECK(errno == EBUSY);
    CHECK(worker_destroys == destroys_before + 1);

    set_pending(threaded_code);
    dispatch(ASIC_IRQB);
    CHECK(threaded_calls == 1);
    CHECK(mask_calls == 1);
    CHECK(unmask_calls == 1);
    CHECK(worker_destroys == destroys_before + 1);

    asic_evt_remove_handler(threaded_code);
    CHECK(worker_destroys == destroys_before + 2);
}

static void test_shutdown_drain(void) {
    unsigned int destroys_before = worker_destroys;

    CHECK(asic_evt_request_threaded_handler(ASIC_EVT_EXP_PCI, other_handler,
                                            NULL, NULL, NULL) == 0);
    asic_shutdown();
    CHECK(worker_destroys == destroys_before + 1);
    CHECK(irq_slots[EXC_IRQ9].handler == NULL);
    CHECK(irq_slots[EXC_IRQB].handler == NULL);
    CHECK(irq_slots[EXC_IRQD].handler == NULL);
}

int main(void) {
    memset(mmio, 0, sizeof(mmio));
    asic_init();

    test_claim_lifecycle();
    test_admission_and_errors();
    test_threaded_lifecycle();
    test_shutdown_drain();
    CHECK(worker_creates == worker_destroys);

    if(failures) {
        fprintf(stderr, "%u ASIC event checks failed\n", failures);
        return EXIT_FAILURE;
    }

    puts("ASIC event ownership tests passed");
    return EXIT_SUCCESS;
}
