/* KallistiOS ##version##

   timer.c
   Copyright (C) 2000, 2001, 2002 Megan Potter
   Copyright (C) 2023 Falco Girgis
   Copyright (C) 2023, 2024 Paul Cercueil <paul@crapouillou.net>
   Copyright (C) 2026 Joseph Black
*/

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <arch/arch.h>
#include <arch/timer.h>
#include <kos/irq.h>
#include <kos/regfield.h>

/* Register access macros */
#define TIMER8(o)   ( *((volatile uint8_t  *)(TIMER_BASE + (o))) )
#define TIMER16(o)  ( *((volatile uint16_t *)(TIMER_BASE + (o))) )
#define TIMER32(o)  ( *((volatile uint32_t *)(TIMER_BASE + (o))) )

/* Register base address */
#define TIMER_BASE 0xffd80000

/* Register offsets */
#define TOCR    0x00    /* Timer Output Control Register */
#define TSTR    0x04    /* Timer Start Register */
#define TCOR0   0x08    /* Timer Constant Register 0 */
#define TCNT0   0x0c    /* Timer Counter Register 0 */
#define TCR0    0x10    /* Timer Control Register 0 */
#define TCOR1   0x14    /* Timer Constant Register 1 */
#define TCNT1   0x18    /* Timer Counter Register 1 */
#define TCR1    0x1c    /* Timer Control Register 1 */
#define TCOR2   0x20    /* Timer Constant Register 2 */
#define TCNT2   0x24    /* Timer Counter Register 2 */
#define TCR2    0x28    /* Timer Control Register 2 */
#define TCPR2   0x2c    /* Timer Input Capture */

/* Timer Start Register fields */
#define STR2    2   /* TCNT2 Counter Start */
#define STR1    1   /* TCNT1 Counter Start */
#define STR0    0   /* TCNT0 Counter Start */

/* Timer Control Register fields */
#define ICPF    BIT(9)          /* Input Capture Interrupt Flag (TMU2 only) */
#define UNF     BIT(8)          /* Underflow Flag */
#define ICPE    GENMASK(7, 6)   /* Input Capture Control (TMU2 only) */
#define UNIE    BIT(5)          /* Underflow Interrupt Control */
#define CKEG    GENMASK(4, 3)   /* Clock Edge */
#define TPSC    GENMASK(2, 0)   /* Timer Prescalar */

/* Clock divisor value for each TPSC value. */
#define TDIV(div)   (4 << (2 * div))

/* Timer Prescalar TPSC values (Peripheral clock divided by N) */
typedef enum PCK_DIV {
    PCK_DIV_4,      /* Pck/4    => 80ns */
    PCK_DIV_16,     /* Pck/16   => 320ns*/
    PCK_DIV_64,     /* Pck/64   => 1280ns*/
    PCK_DIV_256,    /* Pck/256  => 5120ns*/
    PCK_DIV_1024    /* Pck/1024 => 20480ns*/
} PCK_DIV;

/* Timer TPSC values (4 for highest resolution timings) */
#define TIMER_TPSC      PCK_DIV_4
/* Timer IRQ priority levels (0-15) */
#define TIMER_PRIO      15

/* Peripheral clock rate (~49.9 Mhz).
 * The main clock is not exactly 200 MHz, and has been measured at
 * 199499520 Hz. The peripheral clock is a quarter of that. */
#define TIMER_PCK       (199499520 / 4)

/* Timer registers, indexed by Timer ID. */
static const unsigned tcors[] = { TCOR0, TCOR1, TCOR2 };
static const unsigned tcnts[] = { TCNT0, TCNT1, TCNT2 };
static const unsigned tcrs[] = { TCR0, TCR1, TCR2 };

/*
 * TMU1 is the only channel not reserved by a core KOS facility. The typed
 * owner below prevents legacy KOS timer entry points from reprogramming it
 * while a library or application holds an exclusive claim. Direct register
 * writes necessarily remain outside this protection boundary.
 */
struct timer_channel {
    int channel;
    timer_channel_clock_t clock;
    uint32_t period_ticks;
    unsigned int irq_priority;
    timer_channel_callback_t callback;
    void *callback_data;
    uint64_t expirations;
    bool configured;

    uint32_t saved_tcor;
    uint32_t saved_tcnt;
    uint16_t saved_tcr;
    unsigned int saved_irq_priority;
    irq_cb_t saved_irq_handler;
};

static timer_channel_t *timer_channel_owner;

static bool timer_channel_raw_busy(int which) {
    if(which == TMU1 && timer_channel_owner) {
        errno = EBUSY;
        return true;
    }

    return false;
}

static void timer_irq_priority_set_direct(int which, unsigned int priority) {
    irq_set_priority(IRQ_SRC_TMU0 - which, priority);
}

static int timer_channel_clock_params(timer_channel_clock_t clock,
                                      uint16_t *tpsc, uint32_t *divisor) {
    switch(clock) {
        case TIMER_CHANNEL_CLOCK_DIV_4:
            if(tpsc)
                *tpsc = PCK_DIV_4;
            break;
        case TIMER_CHANNEL_CLOCK_DIV_16:
            if(tpsc)
                *tpsc = PCK_DIV_16;
            break;
        case TIMER_CHANNEL_CLOCK_DIV_64:
            if(tpsc)
                *tpsc = PCK_DIV_64;
            break;
        case TIMER_CHANNEL_CLOCK_DIV_256:
            if(tpsc)
                *tpsc = PCK_DIV_256;
            break;
        case TIMER_CHANNEL_CLOCK_DIV_1024:
            if(tpsc)
                *tpsc = PCK_DIV_1024;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if(divisor)
        *divisor = (uint32_t)clock;

    return 0;
}

static void timer_channel_irq_handler(irq_t source, irq_context_t *context,
                                      void *data) {
    timer_channel_t *channel = timer_channel_owner;
    timer_channel_callback_t callback;
    void *callback_data;

    (void)source;
    (void)context;
    (void)data;

    if(!channel)
        return;

    /* Acknowledge before invoking user code so a stopped channel is quiet. */
    TIMER16(tcrs[TMU1]) &= ~UNF;
    ++channel->expirations;

    callback = channel->callback;
    callback_data = channel->callback_data;
    if(callback)
        callback(channel, callback_data);
}

/* Apply timer configuration to registers. */
static int timer_prime_apply(int which, uint32_t count, int interrupts) {
    assert(which >= TMU0 && which <= TMU2);

    irq_disable_scoped();

    if(timer_channel_raw_busy(which))
        return -1;

    TIMER32(tcnts[which]) = count;
    TIMER32(tcors[which]) = count;

    TIMER16(tcrs[which]) = TIMER_TPSC;

    /* Enable IRQ generation plus unmask and set priority */
    if(interrupts) {
        TIMER16(tcrs[which]) |= UNIE;
        timer_enable_ints(which);
    }

    return 0;
}

/* Pre-initialize a timer; set values but don't start it.
   "speed" is the number of desired ticks per second. */
int timer_prime(int which, uint32_t speed, int interrupts) {
    uint64_t divisor;
    uint32_t cd;

    if(speed == 0) {
        errno = EINVAL;
        return -1;
    }

    /* Initialize counters; formula is P0/(tps*div) */
    divisor = (uint64_t)speed * TDIV(TIMER_TPSC);
    cd = (uint32_t)(TIMER_PCK / divisor);

    return timer_prime_apply(which, cd, interrupts);
}

/* Works like timer_prime, but takes an interval in milliseconds
   instead of a rate. Used by the primary timer stuff. */
static int timer_prime_wait(int which, uint32_t millis, int interrupts) {
    /* Calculate the countdown, formula is P0 * millis/div*1000. We
       rearrange the math a bit here to avoid integer overflows. */
    const uint32_t cd = (uint32_t)(((uint64_t)(TIMER_PCK /
                                TDIV(TIMER_TPSC)) * millis) / 1000);

    return timer_prime_apply(which, cd, interrupts);
}

/* Start a timer -- starts it running (and interrupts if applicable) */
int timer_start(int which) {
    assert(which >= TMU0 && which <= TMU2);

    irq_disable_scoped();

    if(timer_channel_raw_busy(which))
        return -1;

    TIMER8(TSTR) |= BIT(which);
    return 0;
}

/* Stop a timer -- and disables its interrupt */
int timer_stop(int which) {
    assert(which >= TMU0 && which <= TMU2);

    irq_disable_scoped();

    if(timer_channel_raw_busy(which))
        return -1;

    timer_disable_ints(which);

    /* Stop timer */
    TIMER8(TSTR) &= ~BIT(which);

    return 0;
}

int timer_running(int which) {
    assert(which >= TMU0 && which <= TMU2);

    return !!(TIMER8(TSTR) & BIT(which));
}

/* Returns the count value of a timer */
uint32_t timer_count(int which) {
    assert(which >= TMU0 && which <= TMU2);

    return TIMER32(tcnts[which]);
}

/* Clears the timer underflow bit and returns what its value was */
int timer_clear(int which) {
    uint16_t value;

    assert(which >= TMU0 && which <= TMU2);

    irq_disable_scoped();

    if(timer_channel_raw_busy(which))
        return -1;
    value = TIMER16(tcrs[which]);

    TIMER16(tcrs[which]) &= ~UNF;
    return !!(value & UNF);
}

/* Enable timer interrupts; needs to move to irq.c sometime. */
void timer_enable_ints(int which) {
    assert(which >= TMU0 && which <= TMU2);

    irq_disable_scoped();

    if(timer_channel_raw_busy(which))
        return;

    timer_irq_priority_set_direct(which, TIMER_PRIO);
}

/* Disable timer interrupts; needs to move to irq.c sometime. */
void timer_disable_ints(int which) {
    assert(which >= TMU0 && which <= TMU2);

    irq_disable_scoped();

    if(timer_channel_raw_busy(which))
        return;

    timer_irq_priority_set_direct(which, IRQ_PRIO_MASKED);
}

/* Check whether ints are enabled */
int timer_ints_enabled(int which) {
    assert(which >= TMU0 && which <= TMU2);

    return irq_get_priority(IRQ_SRC_TMU0 - which) > 0;
}

timer_channel_t *timer_channel_claim(int channel) {
    timer_channel_t *owner;
    irq_mask_t old;

    if(irq_inside_int()) {
        errno = EPERM;
        return NULL;
    }

    if(channel < TMU0 || channel > TMU2) {
        errno = EINVAL;
        return NULL;
    }

    if(channel != TMU1) {
        errno = EBUSY;
        return NULL;
    }

    owner = calloc(1, sizeof(*owner));
    if(!owner)
        return NULL;

    old = irq_disable();
    if(timer_channel_owner || (TIMER8(TSTR) & BIT(TMU1))) {
        irq_restore(old);
        free(owner);
        errno = EBUSY;
        return NULL;
    }

    owner->channel = TMU1;
    owner->saved_tcor = TIMER32(tcors[TMU1]);
    owner->saved_tcnt = TIMER32(tcnts[TMU1]);
    owner->saved_tcr = TIMER16(tcrs[TMU1]);
    owner->saved_irq_priority = irq_get_priority(IRQ_SRC_TMU1);
    owner->saved_irq_handler = irq_get_handler(EXC_TMU1_TUNI1);

    timer_irq_priority_set_direct(TMU1, IRQ_PRIO_MASKED);
    TIMER8(TSTR) &= ~BIT(TMU1);
    TIMER16(tcrs[TMU1]) &= ~(UNIE | UNF);
    irq_set_handler(EXC_TMU1_TUNI1, timer_channel_irq_handler, NULL);
    timer_channel_owner = owner;
    irq_restore(old);

    return owner;
}

int timer_channel_configure(timer_channel_t *channel,
                            const timer_channel_config_t *config) {
    uint16_t tpsc;

    if(!channel || !config || config->period_ticks == 0 ||
       (config->callback && (config->irq_priority == 0 ||
                             config->irq_priority > 15))) {
        errno = EINVAL;
        return -1;
    }

    if(timer_channel_clock_params(config->clock, &tpsc, NULL) < 0)
        return -1;

    irq_disable_scoped();

    if(channel != timer_channel_owner) {
        errno = EINVAL;
        return -1;
    }

    if(TIMER8(TSTR) & BIT(TMU1)) {
        errno = EBUSY;
        return -1;
    }

    timer_irq_priority_set_direct(TMU1, IRQ_PRIO_MASKED);
    TIMER32(tcors[TMU1]) = config->period_ticks - 1u;
    TIMER32(tcnts[TMU1]) = config->period_ticks - 1u;
    TIMER16(tcrs[TMU1]) = tpsc | (config->callback ? UNIE : 0);

    channel->clock = config->clock;
    channel->period_ticks = config->period_ticks;
    channel->irq_priority = config->irq_priority;
    channel->callback = config->callback;
    channel->callback_data = config->callback_data;
    channel->expirations = 0;
    channel->configured = true;

    return 0;
}

int timer_channel_start(timer_channel_t *channel) {
    irq_disable_scoped();

    if(!channel || channel != timer_channel_owner) {
        errno = EINVAL;
        return -1;
    }

    if(!channel->configured) {
        errno = EINVAL;
        return -1;
    }

    TIMER16(tcrs[TMU1]) &= ~UNF;
    if(channel->callback)
        timer_irq_priority_set_direct(TMU1, channel->irq_priority);
    TIMER8(TSTR) |= BIT(TMU1);

    return 0;
}

int timer_channel_stop(timer_channel_t *channel) {
    irq_disable_scoped();

    if(!channel || channel != timer_channel_owner) {
        errno = EINVAL;
        return -1;
    }

    TIMER8(TSTR) &= ~BIT(TMU1);
    timer_irq_priority_set_direct(TMU1, IRQ_PRIO_MASKED);

    return 0;
}

int timer_channel_get_info(const timer_channel_t *channel,
                           timer_channel_info_t *info) {
    uint32_t remaining = 0;

    if(!channel || !info) {
        errno = EINVAL;
        return -1;
    }

    irq_disable_scoped();

    if(channel != timer_channel_owner) {
        errno = EINVAL;
        return -1;
    }

    if(channel->configured)
        remaining = TIMER32(tcnts[TMU1]) + 1u;

    *info = (timer_channel_info_t) {
        .channel = channel->channel,
        .clock = channel->clock,
        .period_ticks = channel->period_ticks,
        .remaining_ticks = remaining,
        .expirations = channel->expirations,
        .configured = channel->configured,
        .running = !!(TIMER8(TSTR) & BIT(TMU1)),
    };

    return 0;
}

int timer_channel_release(timer_channel_t *channel) {
    irq_mask_t old;

    if(!channel) {
        errno = EINVAL;
        return -1;
    }

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    old = irq_disable();
    if(channel != timer_channel_owner) {
        irq_restore(old);
        errno = EINVAL;
        return -1;
    }

    TIMER8(TSTR) &= ~BIT(TMU1);
    timer_irq_priority_set_direct(TMU1, IRQ_PRIO_MASKED);
    TIMER32(tcors[TMU1]) = channel->saved_tcor;
    TIMER32(tcnts[TMU1]) = channel->saved_tcnt;
    TIMER16(tcrs[TMU1]) = channel->saved_tcr;
    irq_set_handler(EXC_TMU1_TUNI1, channel->saved_irq_handler.hdl,
                    channel->saved_irq_handler.data);
    timer_irq_priority_set_direct(TMU1, channel->saved_irq_priority);
    timer_channel_owner = NULL;
    irq_restore(old);

    free(channel);
    return 0;
}

int timer_channel_ticks_to_ns(timer_channel_clock_t clock, uint32_t ticks,
                              uint64_t *nanoseconds) {
    uint32_t divisor;
    uint64_t scaled;
    uint64_t whole;
    uint64_t remainder;

    if(!nanoseconds) {
        errno = EINVAL;
        return -1;
    }

    if(timer_channel_clock_params(clock, NULL, &divisor) < 0)
        return -1;

    scaled = (uint64_t)ticks * divisor;
    whole = scaled / TIMER_PCK;
    remainder = scaled % TIMER_PCK;
    *nanoseconds = whole * 1000000000ULL +
                   remainder * 1000000000ULL / TIMER_PCK;

    return 0;
}

int timer_channel_ns_to_ticks(timer_channel_clock_t clock,
                              uint64_t nanoseconds, uint32_t *ticks) {
    uint32_t divisor;
    uint64_t denominator;
    uint64_t reduced_clock = TIMER_PCK;
    uint64_t common = 320;
    uint64_t whole;
    uint64_t remainder;
    uint64_t result;

    if(!ticks) {
        errno = EINVAL;
        return -1;
    }

    if(timer_channel_clock_params(clock, NULL, &divisor) < 0)
        return -1;

    /* TIMER_PCK and every supported divisor*1e9 denominator share 320. */
    denominator = (uint64_t)divisor * 1000000000ULL / common;
    reduced_clock /= common;
    whole = nanoseconds / denominator;
    remainder = nanoseconds % denominator;

    if(whole > UINT32_MAX / reduced_clock) {
        errno = EOVERFLOW;
        return -1;
    }

    result = whole * reduced_clock;
    if(remainder) {
        uint64_t fraction = remainder * reduced_clock / denominator;

        if(remainder * reduced_clock % denominator)
            ++fraction;
        result += fraction;
    }

    if(result > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    *ticks = (uint32_t)result;
    return 0;
}

int timer_channel_elapsed_ticks(uint32_t start_remaining,
                                uint32_t end_remaining,
                                uint32_t period_ticks,
                                uint32_t *elapsed_ticks) {
    if(!elapsed_ticks || period_ticks == 0 || start_remaining == 0 ||
       end_remaining == 0 || start_remaining > period_ticks ||
       end_remaining > period_ticks) {
        errno = EINVAL;
        return -1;
    }

    if(start_remaining >= end_remaining)
        *elapsed_ticks = start_remaining - end_remaining;
    else
        *elapsed_ticks = start_remaining + (period_ticks - end_remaining);

    return 0;
}

/* Seconds elapsed (since KOS startup), updated from the TMU2 underflow ISR */
static volatile uint32_t timer_ms_counter = 0;
/* Max counter value (used as TMU2 reload), to target a 1 second interval */
static          uint32_t timer_ms_countdown;

/* TMU2 interrupt handler, called every second. Simply updates our
   running second counter and clears the underflow flag. */
static void timer_ms_handler(irq_t source, irq_context_t *context, void *data) {
    (void)source;
    (void)context;
    (void)data;

    timer_ms_counter++;

    /* Clear overflow bit so we can check it when returning time */
    TIMER16(tcrs[TMU2]) &= ~UNF;
}

static void timer_ms_enable(void) {
    irq_set_handler(EXC_TMU2_TUNI2, timer_ms_handler, NULL);
    timer_prime(TMU2, 1, 1);
    timer_ms_countdown = timer_count(TMU2);
    timer_clear(TMU2);
    timer_start(TMU2);
}

/* Generic function for retrieving the current time maintained by TMU2.
   Returns the total amount of time that has elapsed since KOS has been
   initialized, in seconds + ticks. */
timer_val_t __dreamcast_get_ticks(void) {
    uint32_t secs, unf1, unf2, counter1, counter2, delta;
    uint16_t tmu2;

    do {
        /* Read the underflow flag twice, and the counter twice.
           - If both flags are set, it's just unrealistic that one
             second elapsed between the two reads, therefore we can
             assume that the interrupt did not fire yet, and both
             the timer value and the computation of "secs" are valid.
           - If one underflow flag is set, and the other is not,
             the timer value or the "secs" value cannot be trusted;
             loop and try again.
           - If both flags are cleared, either the timer did not
             underflow, or it did but the interrupt handler was quick
             enough to clear the flag, in which case the computation
             of "secs" may be wrong. We can check that by reading
             the timer value again, and if it's above the previous
             value, the timer underflowed and we have to try again.

           This complex setup avoids the issue where the timer
           underflows between the moment where you compute the
           seconds value, and the moment where you read the timer.
           It also does not require the interrupts to be masked. */
        counter1 = TIMER32(tcnts[TMU2]);
        tmu2 = TIMER16(tcrs[TMU2]);
        unf1 = !!(tmu2 & UNF);
        secs = timer_ms_counter + unf1;

        counter2 = TIMER32(tcnts[TMU2]);
        tmu2 = TIMER16(tcrs[TMU2]);
        unf2 = !!(tmu2 & UNF);
    } while(__predict_false(unf1 != unf2 || counter1 < counter2));

    delta = timer_ms_countdown - counter2;

    return (timer_val_t){ .secs = secs, .ticks = delta, };
}

/* Primary kernel timer. What we'll do here is handle actual timer IRQs
   internally, and call the callback only after the appropriate number of
   millis has passed. For the DC you can't have timers spaced out more
   than about one second, so we emulate longer waits with a counter. */
static timer_primary_callback_t tp_callback;
static uint32_t tp_ms_remaining;

/* IRQ handler for the primary timer interrupt. */
static void tp_handler(irq_t src, irq_context_t *cxt, void *data) {
    (void)src;
    (void)data;

    /* Are we at zero? */
    if(tp_ms_remaining == 0) {
        /* Disable any further timer events. The callback may
           re-enable them of course. */
        timer_stop(TMU0);
        timer_disable_ints(TMU0);

        /* Call the callback, if any */
        if(tp_callback)
            tp_callback(cxt);
    }
    /* Do we have less than a second remaining? */
    else if(tp_ms_remaining < 1000) {
        /* Schedule a "last leg" timer. */
        timer_stop(TMU0);
        timer_prime_wait(TMU0, tp_ms_remaining, 1);
        timer_clear(TMU0);
        timer_start(TMU0);
        tp_ms_remaining = 0;
    }
    /* Otherwise, we're just counting down. */
    else {
        tp_ms_remaining -= 1000;
    }
}

/* Enable / Disable primary kernel timer */
static void timer_primary_init(void) {
    /* Clear out our vars */
    tp_callback = NULL;

    /* Clear out TMU0 and get ready for wakeups */
    irq_set_handler(EXC_TMU0_TUNI0, tp_handler, NULL);
    timer_clear(TMU0);
}

static void timer_primary_shutdown(void) {
    timer_stop(TMU0);
    timer_disable_ints(TMU0);
    irq_set_handler(EXC_TMU0_TUNI0, NULL, NULL);
}

timer_primary_callback_t timer_primary_set_callback(timer_primary_callback_t cb) {
    timer_primary_callback_t cbold = tp_callback;
    tp_callback = cb;
    return cbold;
}

void timer_primary_wakeup(uint32_t millis) {
    /* Don't allow zero */
    if(millis == 0) {
        assert_msg(millis != 0, "Received invalid wakeup delay");
        millis++;
    }

    /* Make sure we stop any previous wakeup */
    timer_stop(TMU0);

    /* If we have less than a second to wait, then just schedule the
       timeout event directly. Otherwise schedule a periodic second
       timer. We'll replace this on the last leg in the IRQ. */
    if(millis >= 1000) {
        timer_prime_wait(TMU0, 1000, 1);
        timer_clear(TMU0);
        timer_start(TMU0);
        tp_ms_remaining = millis - 1000;
    }
    else {
        timer_prime_wait(TMU0, millis, 1);
        timer_clear(TMU0);
        timer_start(TMU0);
        tp_ms_remaining = 0;
    }
}

/* Init */
int timer_init(void) {
    /* Disable all timers */
    TIMER8(TSTR) = 0;

    /* Set to internal clock source */
    TIMER8(TOCR) = 0;

    /* Setup the primary timer stuff */
    timer_primary_init();

    /* Setup the 1 HZ timer */
    timer_ms_enable();

    return 0;
}

/* Shutdown */
void timer_shutdown(void) {
    /* Release optional TMU1 ownership before tearing down the IRQ system. */
    if(timer_channel_owner)
        timer_channel_release(timer_channel_owner);

    /* Shutdown primary timer stuff */
    timer_primary_shutdown();

    /* Disable all timers */
    TIMER8(TSTR) = 0;
    timer_disable_ints(TMU0);
    timer_disable_ints(TMU1);
    timer_disable_ints(TMU2);
}
