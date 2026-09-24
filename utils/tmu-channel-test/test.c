/* Copyright (C) 2026 Joseph Black */
#undef calloc
#undef free
#include <string.h>

static unsigned checks, callbacks;
#define CHECK(test) do { ++checks; if(!(test)) { \
    fprintf(stderr, "TMU-TEST: FAIL line=%d %s errno=%d\n", __LINE__, #test, errno); \
    abort(); } } while(0)
#define ERROR(test, code) do { errno = 0; CHECK(test); CHECK(errno == (code)); } while(0)

irq_mask_t irq_disable(void) { unsigned old = masked; masked = 1; return old; }
void irq_restore(irq_mask_t old) { masked = old; }
bool irq_inside_int(void) { return irq_context != 0; }
int irq_set_priority(unsigned source, unsigned priority) {
    CHECK(masked && source < 4 && priority <= 15);
    priorities[source] = priority;
    return 0;
}
unsigned irq_get_priority(unsigned source) { CHECK(source < 4); return priorities[source]; }
int irq_set_handler(irq_t source, irq_hdl_t handler, void *data) {
    CHECK(masked && source >= 0x400 && source <= 0x440);
    handlers[(source - 0x400) / 0x20] = (irq_cb_t){handler, data};
    return 0;
}
irq_cb_t irq_get_handler(irq_t source) { return handlers[(source - 0x400) / 0x20]; }
static void *test_calloc(size_t n, size_t size) {
    CHECK(!masked && !irq_context);
    if(fail_alloc) { errno = ENOMEM; return NULL; }
    ++allocated;
    return calloc(n, size);
}
static void test_free(void *ptr) { CHECK(!masked && !irq_context); ++freed; free(ptr); }
static void saved_handler(irq_t source, irq_context_t *context, void *data) {
    (void)source; (void)context; (void)data;
}
static void callback(timer_channel_t *channel, void *data) {
    CHECK(irq_context && masked && data == &callbacks);
    CHECK(!(test_regs16[TCR1] & UNF));
    ++callbacks;
    if(callbacks == 3) {
        ERROR(timer_channel_release(channel) == -1, EPERM);
        CHECK(timer_channel_stop(channel) == 0);
    }
}

static void ownership(void) {
    timer_channel_info_t info;
    timer_channel_config_t config = { TIMER_CHANNEL_CLOCK_DIV_16, 17, 7, callback, &callbacks };
    timer_channel_t *channel;
    test_regs8[TSTR] = BIT(TMU0) | BIT(TMU2);
    test_regs32[TCOR0] = 100; test_regs32[TCNT0] = 70;
    test_regs32[TCOR2] = 200; test_regs32[TCNT2] = 140;
    test_regs32[TCOR1] = 1234; test_regs32[TCNT1] = 321;
    test_regs16[TCR1] = UNIE | PCK_DIV_64;
    priorities[IRQ_SRC_TMU1] = 9;
    handlers[1] = (irq_cb_t){saved_handler, &info};
    ERROR(timer_channel_claim(-1) == NULL, EINVAL);
    ERROR(timer_channel_claim(3) == NULL, EINVAL);
    ERROR(timer_channel_claim(TMU0) == NULL, EBUSY);
    ERROR(timer_channel_claim(TMU2) == NULL, EBUSY);
    irq_context = 1;
    ERROR(timer_channel_claim(TMU1) == NULL, EPERM);
    irq_context = 0;
    fail_alloc = true;
    ERROR(timer_channel_claim(TMU1) == NULL, ENOMEM);
    fail_alloc = false;
    test_regs8[TSTR] |= BIT(TMU1);
    ERROR(timer_channel_claim(TMU1) == NULL, EBUSY);
    test_regs8[TSTR] &= ~BIT(TMU1);

    test_regs16[TCR1] |= UNF;
    ERROR(timer_channel_claim(TMU1) == NULL, EBUSY);
    CHECK(test_regs16[TCR1] == (UNF | UNIE | PCK_DIV_64));
    CHECK(priorities[IRQ_SRC_TMU1] == 9 && handlers[1].hdl == saved_handler);
    CHECK(timer_clear(TMU1) == 1);
    channel = timer_channel_claim(TMU1);
    CHECK(channel && !masked);
    ERROR(timer_channel_claim(TMU1) == NULL, EBUSY);
    ERROR(timer_channel_start(channel) == -1, EINVAL);
    CHECK(timer_channel_get_info(channel, &info) == 0 && !info.configured);

    uint8_t r8[sizeof(test_regs8)];
    uint16_t r16[0x30]; uint32_t r32[0x30];
    memcpy(r8, test_regs8, sizeof(r8)); memcpy(r16, test_regs16, sizeof(r16));
    memcpy(r32, test_regs32, sizeof(r32));
    ERROR(timer_prime(TMU1, 60, 1) == -1, EBUSY);
    ERROR(timer_start(TMU1) == -1, EBUSY);
    ERROR(timer_stop(TMU1) == -1, EBUSY);
    ERROR(timer_clear(TMU1) == -1, EBUSY);
    errno = 0; timer_enable_ints(TMU1); CHECK(errno == EBUSY);
    errno = 0; timer_disable_ints(TMU1); CHECK(errno == EBUSY);
    CHECK(!memcmp(r8, test_regs8, sizeof(r8)) && !memcmp(r16, test_regs16, sizeof(r16)) &&
          !memcmp(r32, test_regs32, sizeof(r32)));
    CHECK(priorities[IRQ_SRC_TMU1] == 0);

    config.period_ticks = 0;
    ERROR(timer_channel_configure(channel, &config) == -1, EINVAL);
    config.period_ticks = 17; config.irq_priority = 16;
    ERROR(timer_channel_configure(channel, &config) == -1, EINVAL);
    config.irq_priority = 7; config.clock = 3;
    ERROR(timer_channel_configure(channel, &config) == -1, EINVAL);
    config.clock = TIMER_CHANNEL_CLOCK_DIV_16;
    CHECK(timer_channel_configure(channel, &config) == 0);
    CHECK(test_regs32[TCOR1] == 16 && test_regs32[TCNT1] == 16);
    CHECK(test_regs16[TCR1] == (UNIE | PCK_DIV_16));
    CHECK(timer_channel_start(channel) == 0);
    ERROR(timer_channel_configure(channel, &config) == -1, EBUSY);
    test_regs16[TCR1] |= UNF;
    test_regs32[TCNT1] = 8;
    CHECK(timer_channel_start(channel) == 0);
    CHECK((test_regs16[TCR1] & UNF) && test_regs32[TCNT1] == 8);
    for(unsigned i = 0; i < 3; ++i) {
        test_regs16[TCR1] |= UNF;
        masked = irq_context = 1;
        handlers[1].hdl(EXC_TMU1_TUNI1, NULL, handlers[1].data);
        masked = irq_context = 0;
    }
    CHECK(timer_channel_get_info(channel, &info) == 0);
    CHECK(info.expirations == 3 && !info.running && info.remaining_ticks == 9);

    config.callback = NULL; config.irq_priority = 99; config.period_ticks = UINT32_MAX;
    CHECK(timer_channel_configure(channel, &config) == 0);
    CHECK(test_regs32[TCOR1] == UINT32_MAX - 1 && !(test_regs16[TCR1] & UNIE));
    CHECK(timer_channel_get_info(channel, &info) == 0 && info.remaining_ticks == UINT32_MAX);
    CHECK(timer_channel_start(channel) == 0 && priorities[IRQ_SRC_TMU1] == 0);
    CHECK(timer_channel_release(channel) == 0);
    CHECK(timer_channel_owner == NULL && !masked);
    CHECK(test_regs8[TSTR] == (BIT(TMU0) | BIT(TMU2)));
    CHECK(test_regs32[TCOR1] == 1234 && test_regs32[TCNT1] == 321);
    CHECK(test_regs16[TCR1] == (UNIE | PCK_DIV_64));
    CHECK(priorities[IRQ_SRC_TMU1] == 9 && handlers[1].hdl == saved_handler && handlers[1].data == &info);
    CHECK(test_regs32[TCOR0] == 100 && test_regs32[TCNT0] == 70);
    CHECK(test_regs32[TCOR2] == 200 && test_regs32[TCNT2] == 140);
    ERROR(timer_prime(TMU1, 0, 0) == -1, EINVAL);
    CHECK(timer_prime(TMU1, UINT32_MAX, 0) == 0 && timer_count(TMU1) == 0);
    CHECK(timer_start(TMU1) == 0 && timer_stop(TMU1) == 0);
    CHECK(allocated == freed);
}

static void conversions(void) {
    __extension__ typedef unsigned __int128 wide_t;
    const unsigned clocks[] = {4, 16, 64, 256, 1024};
    uint64_t random = UINT64_C(0x123456789abcdef);
    for(unsigned c = 0; c < 5; ++c) {
        wide_t denominator = (wide_t)clocks[c] * 1000000000;
        for(unsigned i = 0; i < 1000; ++i) {
            random = random * UINT64_C(6364136223846793005) + 1;
            uint64_t ns = i < 3 ? (uint64_t[]){0, 1, UINT64_MAX}[i] : random;
            uint32_t ticks = 123;
            wide_t expected = ((wide_t)ns * TIMER_PCK + denominator - 1) / denominator;
            if(expected > UINT32_MAX) {
                ERROR(timer_channel_ns_to_ticks(clocks[c], ns, &ticks) == -1, EOVERFLOW);
                CHECK(ticks == 123);
            }
            else {
                CHECK(timer_channel_ns_to_ticks(clocks[c], ns, &ticks) == 0);
                CHECK(ticks == (uint32_t)expected);
            }
            ticks = (uint32_t)random;
            CHECK(timer_channel_ticks_to_ns(clocks[c], ticks, &ns) == 0);
            CHECK(ns == (uint64_t)((wide_t)ticks * denominator / TIMER_PCK));
            CHECK(timer_channel_ns_to_ticks(clocks[c], ns, &ticks) == 0);
            CHECK(ticks == (uint32_t)random);
        }
    }
    uint32_t elapsed = 123; uint64_t ns;
    ERROR(timer_channel_ns_to_ticks(3, 1, &elapsed) == -1, EINVAL);
    ERROR(timer_channel_ticks_to_ns(3, 1, &ns) == -1, EINVAL);
    ERROR(timer_channel_ns_to_ticks(4, 1, NULL) == -1, EINVAL);
    ERROR(timer_channel_ticks_to_ns(4, 1, NULL) == -1, EINVAL);
    for(uint32_t p = 1; p < 18; ++p) {
        for(uint32_t a = 1; a <= p; ++a) {
            for(uint32_t b = 1; b <= p; ++b) {
                CHECK(timer_channel_elapsed_ticks(a, b, p, &elapsed) == 0);
                CHECK(elapsed == (a + p - b) % p);
            }
        }
    }
    ERROR(timer_channel_elapsed_ticks(0, 1, 2, &elapsed) == -1, EINVAL);
    ERROR(timer_channel_elapsed_ticks(1, 3, 2, &elapsed) == -1, EINVAL);
    CHECK(timer_channel_elapsed_ticks(1, UINT32_MAX, UINT32_MAX, &elapsed) == 0 && elapsed == 1);
}

int main(void) {
    ownership();
    conversions();
    printf("TMU-CHANNEL: PASS checks=%u callbacks=%u allocations=%u frees=%u (host register model)\n",
           checks, callbacks, allocated, freed);
    return 0;
}
