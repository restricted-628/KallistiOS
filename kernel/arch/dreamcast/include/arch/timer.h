/* KallistiOS ##version##

   arch/dreamcast/include/timer.h
   Copyright (c) 2000, 2001 Megan Potter
   Copyright (c) 2023 Falco Girgis
   Copyright (c) 2024 Paul Cercueil
   Copyright (C) 2026 Joseph Black

*/

/** \file    arch/timer.h
    \brief   Low-level timer functionality.
    \ingroup timers

    This file contains functions for interacting with the timer sources on the
    SH4. Many of these functions may interfere with thread operation or other
    such things, and should thus be used with caution. Basically, the only
    functionality that you might use in practice in here in normal programs is
    the gettime functions.

    \sa arch/rtc.h
    \sa dc/wdt.h

    \author Megan Potter
    \author Falco Girgis
*/

#ifndef __ARCH_TIMER_H
#define __ARCH_TIMER_H


#include <stdint.h>
#include <stdbool.h>
#include <kos/cdefs.h>
__BEGIN_DECLS

#include <kos/irq.h>

#include <time.h>

/** \defgroup timers    Timer Unit
    \brief              SH4 CPU peripheral providing timers and counters
    \ingroup            timing

    The Dreamcast's SH4 includes an on-chip Timer Unit (TMU) containing 3
    independent 32-bit channels (TMU0-TMU2). Each channel provides a
    down-counter with automatic reload and can be configured to use 1 of
    7 divider circuits for its input clock. By default, KOS uses the fastest
    input clock for each TMU channel, providing a maximum internal resolution
    of 80ns ticks.

    \note
    KOS reserves TMU0 for scheduling and TMU2 for uptime. TMU1 is available
    through the exclusive timer-channel API or the legacy direct-access API.

    \note
    90% of the time, you will never have a need to directly interact with this
    API, as it's mostly used as a kernel-level driver which backs other APIs.
    For example, querying for ticks, fetching the current timestamp, or putting
    a thread to sleep is typically done via the standard C, C++, or POSIX APIs.

*/

/** \defgroup tmus   Channels
    \brief           TMU channel constants
    \ingroup         timers

    The following are the constant `#define` identifiers for the 3 TMU channels.

    \warning
    All three of these channels are typically reserved and are by KOS for
    OS-related tasks.

    @{
*/

/** \brief  SH4 Timer Channel 0.

    \warning
    This timer is used by the kernel's scheduler for thread operation, and thus
    is off limits if you want that to work properly.
*/
#define TMU0    0

/** \brief  SH4 Timer Channel 1.

    \note
    Prefer timer_channel_claim() when several libraries may use this channel.
    The claim prevents legacy KOS timer calls from silently reprogramming TMU1.
*/
#define TMU1    1

/** \brief  SH4 Timer Channel 2.

    \warning
    This timer channel is used by the various gettime functions in this header.
    It also backs the standard C, C++, and POSIX date/time and clock functions.
*/
#define TMU2    2

/** @} */

/** \cond Which timer does the thread system use? */
#define TIMER_ID TMU0
/** \endcond */

/** \defgroup tmu_direct    Direct-Access
    \brief                  Low-level timer driver
    \ingroup                timers

    This API provides a low-level driver abstraction around the TMU peripheral
    and the control, counter, and reload registers of its 3 channels.

    \note
    You typically want to use the higher-level APIs associated with the
    functionality implemented by each timer channel.
*/

/** \brief   Pre-initialize a timer channel, but do not start it.
    \ingroup tmu_direct

    This function sets up a timer channel for use, but does not start it.

    \param  channel         The timer channel to set up (\ref tmus).
    \param  speed           The number of ticks per second.
    \param  interrupts      Set to 1 to receive interrupts when the timer ticks.
    \retval 0               On success.
    \retval -1              If TMU1 is exclusively claimed or speed is zero.
*/
int timer_prime(int channel, uint32_t speed, int interrupts);

/** \brief   Start a timer channel.
    \ingroup tmu_direct

    This function starts a timer channel that has been initialized with
    timer_prime(), starting raising interrupts if applicable.

    \param  channel         The timer channel to start (\ref tmus).
    \retval 0               On success.
    \retval -1              If TMU1 is exclusively claimed.
*/
int timer_start(int channel);

/** \brief   Stop a timer channel.
    \ingroup tmu_direct

    This function stops a timer channel that was started with timer_start(),
    and as a result stops interrupts coming in from the timer.

    \param  channel         The timer channel to stop (\ref tmus).
    \retval 0               On success.
    \retval -1              If TMU1 is exclusively claimed.
*/
int timer_stop(int channel);

/** \brief   Checks whether a timer channel is running.
    \ingroup tmu_direct

    This function checks whether the given timer channel is actively counting.

    \param  channel         The timer channel to check (\ref tmus).
    \retval 0               The timer channel is stopped.
    \retval 1               The timer channel is running.
*/
int timer_running(int channel);

/** \brief   Obtain the count of a timer channel.
    \ingroup tmu_direct

    This function simply returns the count of the timer channel.

    \param  channel         The timer channel to inspect (\ref tmus).
    \return                 The timer's count.
*/
uint32_t timer_count(int channel);

/** \brief   Clear the underflow bit of a timer channel.
    \ingroup tmu_direct

    This function clears the underflow bit of a timer channel if it was set.

    \param  channel         The timer channel to clear (\ref tmus).
    \retval 0               If the underflow bit was clear (prior to calling).
    \retval 1               If the underflow bit was set (prior to calling).
    \retval -1              If TMU1 is exclusively claimed.
*/
int timer_clear(int channel);

/** \brief   Enable high-priority timer interrupts.
    \ingroup tmu_direct

    This function enables interrupts on the specified timer.

    \param  channel        The timer channel to enable interrupts on (\ref tmus).

    \note                  If TMU1 is exclusively claimed, this function
                           leaves it unchanged and sets `errno` to `EBUSY`.
*/
void timer_enable_ints(int channel);

/** \brief   Disable timer interrupts.
    \ingroup tmu_direct

    This function disables interrupts on the specified timer channel.

    \param  channel         The timer channel to disable interrupts on
                            (\ref tmus).

    \note                  If TMU1 is exclusively claimed, this function
                           leaves it unchanged and sets `errno` to `EBUSY`.
*/
void timer_disable_ints(int channel);

/** \brief   Check whether interrupts are enabled on a timer channel.
    \ingroup tmu_direct

    This function checks whether or not interrupts are enabled on the specified
    timer channel.

    \param  channel         The timer channel to inspect (\ref tmus).
    \retval 0               If interrupts are disabled on the timer.
    \retval 1               If interrupts are enabled on the timer.
*/
int timer_ints_enabled(int channel);

/** \defgroup tmu_channel    Exclusive Timer Channel
    \brief                   Owned, periodic access to TMU1
    \ingroup                 timers

    This API provides typed, exclusive ownership of TMU1. It is intended for
    high-resolution periodic work that cannot use thread-context software
    timers or VBlank callbacks. No memory or interrupt resources are consumed
    until a caller claims the channel.

    While TMU1 is claimed, legacy mutating calls such as timer_prime(),
    timer_start(), timer_stop(), timer_clear(), timer_enable_ints(), and
    timer_disable_ints() refuse to alter it. The two void interrupt-control
    functions leave the channel unchanged and set `errno` to `EBUSY`.

    \warning
    Callbacks execute in interrupt context. They must not block, allocate, or
    call timer_channel_release(). Direct writes to TMU registers bypass KOS and
    therefore cannot be protected by this ownership model.

    @{
*/

/** \brief Opaque exclusive timer-channel handle. */
typedef struct timer_channel timer_channel_t;

/** \brief Supported peripheral-clock divisors. */
typedef enum timer_channel_clock {
    TIMER_CHANNEL_CLOCK_DIV_4 = 4,       /**< Peripheral clock divided by 4. */
    TIMER_CHANNEL_CLOCK_DIV_16 = 16,     /**< Peripheral clock divided by 16. */
    TIMER_CHANNEL_CLOCK_DIV_64 = 64,     /**< Peripheral clock divided by 64. */
    TIMER_CHANNEL_CLOCK_DIV_256 = 256,   /**< Peripheral clock divided by 256. */
    TIMER_CHANNEL_CLOCK_DIV_1024 = 1024  /**< Peripheral clock divided by 1024. */
} timer_channel_clock_t;

/** \brief Periodic timer-channel callback.

    \param  channel         The channel which underflowed.
    \param  data            User data supplied in timer_channel_config_t.
*/
typedef void (*timer_channel_callback_t)(timer_channel_t *channel, void *data);

/** \brief Exclusive channel configuration. */
typedef struct timer_channel_config {
    timer_channel_clock_t clock; /**< Input clock divisor. */
    uint32_t period_ticks;       /**< Logical ticks per period; must be nonzero. */
    unsigned int irq_priority;   /**< IRQ priority from 1 through 15. */
    timer_channel_callback_t callback; /**< Optional periodic IRQ callback. */
    void *callback_data;         /**< User data passed to callback. */
} timer_channel_config_t;

/** \brief Snapshot of an exclusive channel. */
typedef struct timer_channel_info {
    int channel;                 /**< Claimed TMU channel number. */
    timer_channel_clock_t clock; /**< Configured input clock divisor. */
    uint32_t period_ticks;       /**< Logical ticks in one complete period. */
    uint32_t remaining_ticks;    /**< Logical ticks until the next underflow. */
    uint64_t expirations;        /**< Underflows handled since configuration. */
    bool configured;             /**< Whether the channel has a configuration. */
    bool running;                /**< Whether the counter is currently running. */
} timer_channel_info_t;

/** \brief Claim exclusive ownership of a timer channel.

    TMU1 is currently the only claimable channel. TMU0 and TMU2 are reserved by
    KOS and fail with `EBUSY`. A running TMU1 also fails with `EBUSY`; stop any
    legacy use before claiming it. The previous stopped register and interrupt
    configuration is restored by timer_channel_release().

    \param  channel         The channel to claim.
    \return                 A handle on success, or `NULL` with `errno` set.
*/
timer_channel_t *timer_channel_claim(int channel);

/** \brief Configure a claimed timer channel without starting it.

    A logical period of N ticks is programmed as a reload value of N - 1, so
    the callback cadence is exactly N selected input-clock ticks. The channel
    must be stopped. If callback is `NULL`, irq_priority is ignored and the
    channel may be polled through timer_channel_get_info().

    \param  channel         A valid exclusive channel handle.
    \param  config          Configuration to apply.
    \retval 0               On success.
    \retval -1              On error with `errno` set.
*/
int timer_channel_configure(timer_channel_t *channel,
                            const timer_channel_config_t *config);

/** \brief Start a configured exclusive timer channel.

    \param  channel         A valid exclusive channel handle.
    \retval 0               On success.
    \retval -1              On error with `errno` set.
*/
int timer_channel_start(timer_channel_t *channel);

/** \brief Stop an exclusive timer channel.

    This function is safe to call from the channel's own interrupt callback.

    \param  channel         A valid exclusive channel handle.
    \retval 0               On success.
    \retval -1              On error with `errno` set.
*/
int timer_channel_stop(timer_channel_t *channel);

/** \brief Obtain a coherent snapshot of an exclusive timer channel.

    \param  channel         A valid exclusive channel handle.
    \param  info            Destination for the snapshot.
    \retval 0               On success.
    \retval -1              On error with `errno` set.
*/
int timer_channel_get_info(const timer_channel_t *channel,
                           timer_channel_info_t *info);

/** \brief Release an exclusive timer channel.

    The channel is stopped and its pre-claim register, handler, and interrupt
    priority state is restored. This function cannot be called from interrupt
    context.

    \param  channel         A valid exclusive channel handle.
    \retval 0               On success.
    \retval -1              On error with `errno` set.
*/
int timer_channel_release(timer_channel_t *channel);

/** \brief Convert channel ticks to nanoseconds, rounding down.

    \param  clock           Input clock divisor used for the ticks.
    \param  ticks           Number of logical timer ticks.
    \param  nanoseconds     Destination for the converted duration.
    \retval 0               On success.
    \retval -1              For an invalid argument.
*/
int timer_channel_ticks_to_ns(timer_channel_clock_t clock, uint32_t ticks,
                              uint64_t *nanoseconds);

/** \brief Convert nanoseconds to channel ticks, rounding up.

    Rounding up ensures that a programmed interval is never shorter than the
    requested duration. Durations requiring more than UINT32_MAX ticks fail
    with `EOVERFLOW`.

    \param  clock           Input clock divisor to use.
    \param  nanoseconds     Duration to convert.
    \param  ticks           Destination for the logical tick count.
    \retval 0               On success.
    \retval -1              On error with `errno` set.
*/
int timer_channel_ns_to_ticks(timer_channel_clock_t clock,
                              uint64_t nanoseconds, uint32_t *ticks);

/** \brief Calculate elapsed ticks from two remaining-count snapshots.

    Both snapshots use the logical `remaining_ticks` convention from
    timer_channel_info_t and must be in the range 1 through period_ticks. The
    result is modulo one period; multiple complete wraps cannot be inferred
    from counter snapshots alone.

    \param  start_remaining Earlier remaining-tick snapshot.
    \param  end_remaining   Later remaining-tick snapshot.
    \param  period_ticks    Configured logical period.
    \param  elapsed_ticks   Destination for elapsed ticks.
    \retval 0               On success.
    \retval -1              For an invalid argument.
*/
int timer_channel_elapsed_ticks(uint32_t start_remaining,
                                uint32_t end_remaining,
                                uint32_t period_ticks,
                                uint32_t *elapsed_ticks);

/** @} */

/** \defgroup tmu_uptime    Uptime
    \brief                  Maintaining time since system boot.
    \ingroup                timers

    This API provides methods for querying the current system boot time or
    uptime since KOS started at various resolutions. You can use this timing
    for ticks, delta time, or frame deltas for games, profilers, or media
    decoding.

    \note
    This API is used to back the C, C++, and POSIX standard date/time
    APIs. You may wish to favor these for platform independence.

    \warning
    This API and its underlying functionality are using \ref TMU2, so any
    direct manipulation of it will interfere with the API's proper functioning.

    \note
    The highest actual tick resolution of \ref TMU2 is 80ns.
*/

/** \brief   Structure that holds timer values in seconds + ticks. */
typedef struct timer_value {
    uint32_t secs;      /**< \brief Number of seconds */
    uint32_t ticks;     /**< \brief Number of ticks in the current second */
} timer_val_t;

/** \brief   Get the current uptime of the system (in seconds and ticks).
    \ingroup tmu_uptime

    This function retrieves the number of seconds and ticks since KOS was
    started.

    \return                 The time since KOS started as a timer_val_t.
*/
timer_val_t __dreamcast_get_ticks(void);

/** \brief   Get the current uptime of the system (in seconds and nanoseconds).
    \ingroup tmu_uptime

    This function retrieves the time since KOS was started, in a standard
    timespec format.

    \return                 The time since KOS started as a struct timespec.
*/
static inline struct timespec arch_timer_gettime(void) {
    timer_val_t time = __dreamcast_get_ticks();

    /* Convert from ticks to nanoseconds: each clock tick is 80ns. */
    return (struct timespec){
        .tv_sec = time.secs,
        .tv_nsec = (long int)(time.ticks * 80),
    };
}

/** \defgroup tmu_primary   Primary Timer
    \brief                  Primary timer used by the kernel.
    \ingroup                timers

    This API provides a callback notification mechanism that can be hooked into
    the primary timer (TMU0). It is used by the KOS kernel for threading and
    scheduling.

    \warning
    This API and its underlying functionality are using \ref TMU0, so any
    direct manipulation of it will interfere with the API's proper functioning.
*/

/** \brief   Primary timer callback type.
    \ingroup tmu_primary

    This is the type of function which may be passed to
    timer_primary_set_callback() as the function that gets invoked
    upon interrupt.
*/
typedef void (*timer_primary_callback_t)(irq_context_t *);

/** \brief   Set the primary timer callback.
    \ingroup tmu_primary

    This function sets the primary timer callback to the specified function
    pointer.

    \warning
    Generally, you should not do this, as the threading system relies
    on the primary timer to work.

    \param  callback        The new timer callback (set to NULL to disable).
    \return                 The old timer callback.
*/
timer_primary_callback_t timer_primary_set_callback(timer_primary_callback_t callback);

/** \brief   Request a primary timer wakeup.
    \ingroup tmu_primary

    This function will wake the caller (by calling the primary timer callback)
    in approximately the number of milliseconds specified. You can only have one
    timer wakeup scheduled at a time. Any subsequently scheduled wakeups will
    replace any existing one.

    \param  millis          The number of milliseconds to schedule for.
*/
void timer_primary_wakeup(uint32_t millis);

/** \cond */
/* Init function */
int timer_init(void);

/* Shutdown */
void timer_shutdown(void);
/** \endcond */

__END_DECLS

#include <kos/timer.h>

#endif  /* __ARCH_TIMER_H */
