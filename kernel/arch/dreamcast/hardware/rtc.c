/* KallistiOS ##version##

   rtc.c
   Copyright (C) 2001 Megan Potter
   Copyright (C) 2023 Falco Girgis
   Copyright (C) 2023 Ruslan Rostovtsev
   Copyright (C) 2023 Megavolt85
   Copyright (C) 2026 Joseph Black
*/

/*
   Real-Time Clock (RTC) Support

   The functions in here return various info about the real-world time and
   date stored in the machine. The general process here is to retrieve
   the date/time value and then use the other functions to interpret it.

   rtc_get_time() should return a UNIX-epoch time stamp, and then the normal
   BSD library functions can be used to interpret that time stamp.

   For the Dreamcast, the RTC is a 32-bit seconds counter located at
   0xa0710000 and 0xa0710004 (each 32-bits long). 0000 contains the high
   16 bits and 0004 contains the low 16 bits. The epoch of this counter is
   January 1, 1950, 00:00. So we just grab that value and add about
   20 years to it.

 */

#include <arch/rtc.h>
#include <kos/irq.h>
#include <kos/timer.h>
#include <dc/g2bus.h>

#include <stdint.h>
#include <assert.h>
#include <errno.h>

/*
    High 16-bit Timestamp Value

    32-bit register containing the upper 16-bits of
    the 32-bit timestamp in seconds. Only the lower 16-bits
    are valid.

    Writing to this register will lock the timestamp registers.
*/
#define RTC_TIMESTAMP_HIGH_ADDR   0xa0710000

/*
    Low 16-bit Timestamp Value

    32-bit register containing the lower 16-bits of
    the 32-bit timestamp in seconds. Only the lower 16-bits
    are valid.
*/
#define RTC_TIMESTAMP_LOW_ADDR    0xa0710004

/*
    Timestamp Control Register

    All fields are reserved except for RTC_CTRL_WRITE_EN,
    which is write-only.
*/
#define RTC_CTRL_ADDR             0xa0710008

/*
    Timestamp Write Enable

    RTC_CTRL_ADDR field to be written in order to unlock
    writing to the timestamp registers.
*/
#define RTC_CTRL_WRITE_EN         (1 << 0)

/*
    Number of read/write retry attempts

    To ensure a coherent, race-free read/write operation.
*/
#define RTC_RETRY_COUNT         3

/* The boot time; we'll save this in rtc_init() */
time_t dc_boot_time;

/* The G2 bus must remain locked for every call to this helper. Holding that
   lock prevents another thread or interrupt from observing half of a write. */
static uint32_t read_counter_locked(void) {
    uint32_t rtcold, rtcnew;
    int i;

    /* Try several times to make sure we don't read one value, then the
       clock increments itself, then we read the second value. This
       algorithm is from NetBSD. */
    rtcold = 0;

    for(;;) {
        for(i = 0; i < RTC_RETRY_COUNT; i++) {
            rtcnew = ((g2_read_32_raw(RTC_TIMESTAMP_HIGH_ADDR) & 0xffff) <<
                      16) |
                     (g2_read_32_raw(RTC_TIMESTAMP_LOW_ADDR) & 0xffff);

            if(rtcnew != rtcold)
                break;
        }

        if(i < RTC_RETRY_COUNT)
            rtcold = rtcnew;
        else
            break;
    }

    return rtcnew;
}

static void set_boot_time(time_t value) {
    irq_mask_t irq = irq_disable();

    dc_boot_time = value;
    irq_restore(irq);
}

int arch_rtc_get_counter(uint32_t *counter) {
    g2_ctx_t g2;

    if(!counter) {
        errno = EFAULT;
        return -1;
    }

    g2 = g2_lock();
    *counter = read_counter_locked();
    g2_unlock(g2);
    return 0;
}

int rtc_get_counter(uint32_t *counter) {
    return arch_rtc_get_counter(counter);
}

int arch_rtc_set_counter(uint32_t counter) {
    g2_ctx_t g2;
    uint32_t observed = 0;
    uint32_t elapsed_secs, elapsed_millis;
    int i;

    /* The lock spans the complete protocol. In addition to protecting G2, its
       IRQ mask prevents the scheduler from interleaving two setters between
       the control, low-word, and high-word writes. */
    g2 = g2_lock();
    g2_write_32_raw(RTC_CTRL_ADDR, RTC_CTRL_WRITE_EN);
    g2_fifo_wait();

    for(i = 0; i < RTC_RETRY_COUNT; ++i) {
        /* Writing the high half closes the RTC's write window, so the low half
           must be committed first on every retry. */
        g2_write_32_raw(RTC_TIMESTAMP_LOW_ADDR, counter & 0xffffu);
        g2_write_32_raw(RTC_TIMESTAMP_HIGH_ADDR, counter >> 16);
        g2_fifo_wait();

        observed = read_counter_locked();
        if(observed == counter ||
           (counter != UINT32_MAX && observed == counter + 1u))
            break;
    }

    g2_write_32_raw(RTC_CTRL_ADDR, 0);
    g2_fifo_wait();

    /* CLOCK_REALTIME is the counter value minus elapsed monotonic time. Keep
       this update in the same non-preemptible transaction as the hardware
       write so another setter cannot publish an older cached value afterward. */
    timer_ms_gettime(&elapsed_secs, &elapsed_millis);
    dc_boot_time = ((time_t)observed - RTC_COUNTER_UNIX_EPOCH) -
                   elapsed_secs;
    g2_unlock(g2);

    if(i == RTC_RETRY_COUNT) {
        errno = EPERM;
        return -1;
    }

    return 0;
}

int rtc_set_counter(uint32_t counter) {
    return arch_rtc_set_counter(counter);
}

/* Returns the date/time value as a Unix-epoch timestamp. */
time_t arch_rtc_unix_secs(void) {
    uint32_t counter;

    arch_rtc_get_counter(&counter);
    return (time_t)counter - RTC_COUNTER_UNIX_EPOCH;
}

/* Sets the date/time value from a UNIX epoch time stamp,
   returning 0 for success or -1 for failure. */
int arch_rtc_set_unix_secs(time_t secs) {
    /* Check before adding the epoch delta so an extreme time_t cannot invoke
       signed overflow before it is rejected. */
    if(secs < -(time_t)RTC_COUNTER_UNIX_EPOCH ||
       secs > (time_t)(UINT32_MAX - RTC_COUNTER_UNIX_EPOCH)) {
        errno = EINVAL;
        return -1;
    }

    return arch_rtc_set_counter((uint32_t)(secs +
                                           RTC_COUNTER_UNIX_EPOCH));
}

time_t arch_rtc_boot_time(void) {
    irq_mask_t irq = irq_disable();
    time_t value = dc_boot_time;

    irq_restore(irq);
    return value;
}

int arch_rtc_init(void) {
    set_boot_time(arch_rtc_unix_secs());

    return 0;
}
