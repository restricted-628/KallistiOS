/* KallistiOS ##version##

   include/kos/rtc.h
   Copyright (C) 2000, 2001 Megan Potter
   Copyright (C) 2023, 2024 Falco Girgis
   Copyright (C) 2026 Joseph Black

*/

/** \file    kos/rtc.h
    \brief   Low-level real-time clock functionality.
    \ingroup rtc

    This file contains functions for interacting with the real-time clock.
    Generally, you should prefer interacting with the higher level standard C
    functions, like time(), rather than these when simply needing to fetch the
    current system time.

    \author Megan Potter
    \author Falco Girgis
*/

#ifndef __KOS_RTC_H
#define __KOS_RTC_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>
#include <arch/rtc.h>

#include <time.h>

/** \defgroup rtc Real-Time Clock
    \brief        Real-Time Clock (RTC) Management
    \ingroup      timing

    Provides an API for fetching and managing the date/time using
    the hardware real-time clock (RTC). All timestamps are in standard
    Unix format, with an epoch of January 1, 1970. Due to the fact
    that there is no time zone data on the RTC, all times are expected
    to be in the local time zone.

    \note
    For reading the current date/time, you should favor the standard C,
    C++, or POSIX functions, as they are platform-indpendent and are
    calculating current time based on a cached boot time plus a delta
    that is maintained by the timer subsystem, rather than actually
    having to requery the RTC, so they are faster.

    \sa wdt, timers, perf_counters

    @{
*/

/** \brief Seconds between the RTC and Unix epochs. */
#define RTC_COUNTER_UNIX_EPOCH UINT32_C(631152000)

/** \brief Civil weekday values produced by RTC conversion functions. */
typedef enum rtc_weekday {
    RTC_WEEKDAY_SUNDAY = 0,
    RTC_WEEKDAY_MONDAY,
    RTC_WEEKDAY_TUESDAY,
    RTC_WEEKDAY_WEDNESDAY,
    RTC_WEEKDAY_THURSDAY,
    RTC_WEEKDAY_FRIDAY,
    RTC_WEEKDAY_SATURDAY
} rtc_weekday_t;

/** \brief Calendar representation of an RTC counter value.

    The weekday is derived when converting from a counter. It is ignored when
    converting a calendar value to a counter or setting the RTC.
*/
typedef struct rtc_datetime {
    uint16_t year;          /**< \brief Full Gregorian year. */
    uint8_t month;          /**< \brief Month in the range 1 through 12. */
    uint8_t day;            /**< \brief Day in the range 1 through 31. */
    uint8_t hour;           /**< \brief Hour in the range 0 through 23. */
    uint8_t minute;         /**< \brief Minute in the range 0 through 59. */
    uint8_t second;         /**< \brief Second in the range 0 through 59. */
    uint8_t weekday;        /**< \brief Derived rtc_weekday_t value. */
} rtc_datetime_t;

/** \brief Read the hardware's native 1950-epoch counter.

    \param counter         Output receiving the complete 32-bit counter.

    \retval 0              On success.
    \retval -1             On failure with errno set.

    \exception EFAULT      \p counter was NULL.
*/
int rtc_get_counter(uint32_t *counter);

/** \brief Set the hardware's native 1950-epoch counter.

    The cached boot time used by CLOCK_REALTIME is updated coherently from the
    final stable readback. This keeps realtime synchronized with the hardware
    even when the requested value cannot be verified and the call fails.

    \param counter         Counter value to write.

    \retval 0              On success.
    \retval -1             On failure with errno set.

    \exception EPERM       The value could not be written and read back.
*/
int rtc_set_counter(uint32_t counter);

/** \brief Convert a native RTC counter to a Gregorian calendar value.

    Every 32-bit counter value is representable. The conversion is purely
    arithmetic and does not access the hardware.

    \param counter         Native 1950-epoch counter.
    \param datetime        Output calendar value.

    \retval 0              On success.
    \retval -1             On failure with errno set.

    \exception EFAULT      \p datetime was NULL.
*/
int rtc_counter_to_datetime(uint32_t counter, rtc_datetime_t *datetime);

/** \brief Convert a Gregorian calendar value to a native RTC counter.

    \param datetime        Calendar value to convert. Its weekday is ignored.
    \param counter         Output native 1950-epoch counter.

    \retval 0              On success.
    \retval -1             On failure with errno set.

    \exception EFAULT      Either pointer was NULL.
    \exception EINVAL      A calendar field was invalid.
    \exception ERANGE      The valid date is outside the hardware range.
*/
int rtc_datetime_to_counter(const rtc_datetime_t *datetime,
                            uint32_t *counter);

/** \brief Compare two validated Gregorian calendar values.

    \param lhs             Left calendar operand.
    \param rhs             Right calendar operand.
    \param result          Receives -1, 0, or 1.

    \retval 0              On success.
    \retval -1             On failure with errno set as for
                            rtc_datetime_to_counter().
*/
int rtc_datetime_compare(const rtc_datetime_t *lhs,
                         const rtc_datetime_t *rhs, int *result);

/** \brief Read and convert the current hardware RTC value. */
int rtc_get_datetime(rtc_datetime_t *datetime);

/** \brief Validate, convert, and set the current hardware RTC value. */
int rtc_set_datetime(const rtc_datetime_t *datetime);

/** \brief   Get the current date/time.

    This function retrieves the current RTC value as a standard UNIX timestamp
    (with an epoch of January 1, 1970 00:00). This is assumed to be in the
    timezone of the user (as the RTC does not support timezones).

    \return                 The current UNIX-style timestamp (local time).

    \sa rtc_set_unix_secs()
*/
static inline time_t rtc_unix_secs(void) {
    return arch_rtc_unix_secs();
}

/** \brief   Set the current date/time.

    This function sets the current RTC value as a standard UNIX timestamp
    (with an epoch of January 1, 1970 00:00). This is assumed to be in the
    timezone of the user (as the RTC does not support timezones).

    \warning
    This function may fail! Since `time_t` is typically 64-bit while the RTC
    uses a 32-bit timestamp (which also has a different epoch), not all
    `time_t` values can be represented within the RTC!

    \param      time        Unix timestamp to set the current time to

    \return                 0 for success or -1 for failure (with errno set
                            appropriately).

    \exception  EINVAL      \p time was an invalid timestamp or could not be
                            represented on the hardware RTC.
    \exception  EPERM       Failed to set and successfully read back \p time
                            from the RTC.

    \sa rtc_unix_secs()
*/
static inline int rtc_set_unix_secs(time_t time) {
    return arch_rtc_set_unix_secs(time);
}

/** \brief   Get the time since the system was booted.

    This function retrieves the cached RTC value from when KallistiOS was
    started. As with rtc_unix_secs(), this is a UNIX-style timestamp in
    local time.

    \return                 The boot time as a UNIX-style timestamp.
*/
static inline time_t rtc_boot_time(void) {
    return arch_rtc_boot_time();
}

/* \cond INTERNAL */
/* Internally called Init / Shutdown */
static inline int rtc_init(void) {
    return arch_rtc_init();
}

static inline void rtc_shutdown(void) {
    arch_rtc_shutdown();
}
/* \endcond */

/** @} */

__END_DECLS

#endif  /* __KOS_RTC_H */
