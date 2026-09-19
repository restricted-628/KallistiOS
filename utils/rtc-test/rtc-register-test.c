/* KallistiOS ##version##

   rtc-register-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <arch/rtc.h>
#include <dc/g2bus.h>
#include <kos/irq.h>
#include <kos/timer.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#define RTC_HIGH_ADDR UINT32_C(0xa0710000)
#define RTC_LOW_ADDR  UINT32_C(0xa0710004)
#define RTC_CTRL_ADDR UINT32_C(0xa0710008)

typedef struct write_entry {
    uintptr_t address;
    uint32_t value;
} write_entry_t;

static int failures;
static int g2_depth;
static int access_outside_lock;
static unsigned int irq_depth;
static uint32_t counter_value;
static uint32_t pending_low;
static uint32_t elapsed_seconds;
static uint32_t readback_bias;
static int force_bad_readback;
static write_entry_t writes[32];
static size_t write_count;

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

static void reset_mock(uint32_t counter) {
    g2_depth = 0;
    access_outside_lock = 0;
    irq_depth = 0;
    counter_value = counter;
    pending_low = counter & 0xffffu;
    elapsed_seconds = 0;
    readback_bias = 0;
    force_bad_readback = 0;
    write_count = 0;
}

irq_mask_t irq_disable(void) {
    irq_mask_t old = irq_depth;

    ++irq_depth;
    return old;
}

void irq_restore(irq_mask_t state) {
    irq_depth = state;
}

g2_ctx_t g2_lock(void) {
    g2_ctx_t context = { irq_disable() };

    ++g2_depth;
    return context;
}

void g2_unlock(g2_ctx_t context) {
    --g2_depth;
    irq_restore(context.state);
}

uint32_t g2_read_32_raw(uintptr_t address) {
    uint32_t value = force_bad_readback ? UINT32_C(0x12345678) :
                     counter_value + readback_bias;

    if(g2_depth != 1)
        access_outside_lock = 1;

    if(address == RTC_HIGH_ADDR)
        return value >> 16;
    if(address == RTC_LOW_ADDR)
        return value & 0xffffu;
    return 0;
}

void g2_write_32_raw(uintptr_t address, uint32_t value) {
    if(g2_depth != 1)
        access_outside_lock = 1;

    if(write_count < sizeof(writes) / sizeof(writes[0])) {
        writes[write_count].address = address;
        writes[write_count].value = value;
        ++write_count;
    }

    if(address == RTC_LOW_ADDR)
        pending_low = value & 0xffffu;
    else if(address == RTC_HIGH_ADDR)
        counter_value = ((value & 0xffffu) << 16) | pending_low;
}

void g2_fifo_wait(void) {
    if(g2_depth != 1)
        access_outside_lock = 1;
}

void timer_ms_gettime(uint32_t *seconds, uint32_t *milliseconds) {
    *seconds = elapsed_seconds;
    *milliseconds = 0;
}

static void test_reads_and_boot_time(void) {
    uint32_t counter;

    reset_mock(0);
    CHECK(arch_rtc_get_counter(&counter) == 0);
    CHECK(counter == 0);
    CHECK(arch_rtc_unix_secs() == -(time_t)RTC_COUNTER_UNIX_EPOCH);
    CHECK(!access_outside_lock && !g2_depth && !irq_depth);

    errno = 0;
    CHECK(arch_rtc_get_counter(NULL) < 0);
    CHECK(errno == EFAULT);

    reset_mock(RTC_COUNTER_UNIX_EPOCH);
    CHECK(arch_rtc_init() == 0);
    CHECK(arch_rtc_boot_time() == 0);
    CHECK(!g2_depth && !irq_depth);
}

static void test_write_order_and_tick_acceptance(void) {
    reset_mock(0);
    elapsed_seconds = 7;
    readback_bias = 1;
    CHECK(arch_rtc_set_counter(UINT32_C(0x12345678)) == 0);
    CHECK(write_count == 4);
    CHECK(writes[0].address == RTC_CTRL_ADDR && writes[0].value == 1);
    CHECK(writes[1].address == RTC_LOW_ADDR &&
          writes[1].value == UINT32_C(0x5678));
    CHECK(writes[2].address == RTC_HIGH_ADDR &&
          writes[2].value == UINT32_C(0x1234));
    CHECK(writes[3].address == RTC_CTRL_ADDR && writes[3].value == 0);
    CHECK(arch_rtc_boot_time() ==
          (time_t)UINT32_C(0x12345679) - RTC_COUNTER_UNIX_EPOCH - 7);
    CHECK(!access_outside_lock && !g2_depth && !irq_depth);
}

static void test_unix_bounds_and_failure(void) {
    const time_t minimum = -(time_t)RTC_COUNTER_UNIX_EPOCH;
    const time_t maximum = (time_t)(UINT32_MAX - RTC_COUNTER_UNIX_EPOCH);

    reset_mock(0);
    CHECK(arch_rtc_set_unix_secs(minimum) == 0);
    CHECK(counter_value == 0);

    reset_mock(0);
    CHECK(arch_rtc_set_unix_secs(maximum) == 0);
    CHECK(counter_value == UINT32_MAX);

    reset_mock(0);
    errno = 0;
    CHECK(arch_rtc_set_unix_secs(minimum - 1) < 0);
    CHECK(errno == EINVAL && write_count == 0);

    reset_mock(0);
    errno = 0;
    CHECK(arch_rtc_set_unix_secs(maximum + 1) < 0);
    CHECK(errno == EINVAL && write_count == 0);

    reset_mock(0);
    force_bad_readback = 1;
    errno = 0;
    CHECK(arch_rtc_set_counter(42) < 0);
    CHECK(errno == EPERM);
    CHECK(write_count == 8);
    CHECK(writes[write_count - 1].address == RTC_CTRL_ADDR);
    CHECK(writes[write_count - 1].value == 0);
    CHECK(!access_outside_lock && !g2_depth && !irq_depth);
}

int main(void) {
    test_reads_and_boot_time();
    test_write_order_and_tick_acceptance();
    test_unix_bounds_and_failure();

    if(failures) {
        fprintf(stderr, "%d RTC register test(s) failed\n", failures);
        return 1;
    }

    puts("RTC register tests passed");
    return 0;
}
