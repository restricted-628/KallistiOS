/* KallistiOS ##version##

   rtc_calendar.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos/rtc.h>

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define RTC_EPOCH_YEAR          1950u
#define SECONDS_PER_MINUTE      UINT64_C(60)
#define SECONDS_PER_HOUR        UINT64_C(3600)
#define SECONDS_PER_DAY         UINT64_C(86400)

static const uint8_t month_days[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static int leap_year(uint32_t year) {
    return (!(year % 4u) && (year % 100u)) || !(year % 400u);
}

static uint32_t days_in_month(uint32_t year, uint32_t month) {
    uint32_t days = month_days[month - 1u];

    if(month == 2u && leap_year(year))
        ++days;

    return days;
}

int rtc_counter_to_datetime(uint32_t counter, rtc_datetime_t *datetime) {
    uint32_t days, year, month;
    uint32_t remaining;

    if(!datetime) {
        errno = EFAULT;
        return -1;
    }

    days = counter / (uint32_t)SECONDS_PER_DAY;
    remaining = counter % (uint32_t)SECONDS_PER_DAY;
    datetime->weekday = (uint8_t)(days % 7u);

    year = RTC_EPOCH_YEAR;
    for(;;) {
        uint32_t year_days = leap_year(year) ? 366u : 365u;

        if(days < year_days)
            break;

        days -= year_days;
        ++year;
    }

    month = 1u;
    for(;;) {
        uint32_t current_month_days = days_in_month(year, month);

        if(days < current_month_days)
            break;

        days -= current_month_days;
        ++month;
    }

    datetime->year = (uint16_t)year;
    datetime->month = (uint8_t)month;
    datetime->day = (uint8_t)(days + 1u);
    datetime->hour = (uint8_t)(remaining / (uint32_t)SECONDS_PER_HOUR);
    remaining %= (uint32_t)SECONDS_PER_HOUR;
    datetime->minute = (uint8_t)(remaining /
                                  (uint32_t)SECONDS_PER_MINUTE);
    datetime->second = (uint8_t)(remaining %
                                  (uint32_t)SECONDS_PER_MINUTE);
    return 0;
}

int rtc_datetime_to_counter(const rtc_datetime_t *datetime,
                            uint32_t *counter) {
    uint64_t days = 0;
    uint64_t seconds;
    uint32_t year, month;

    if(!datetime || !counter) {
        errno = EFAULT;
        return -1;
    }

    if(datetime->year < RTC_EPOCH_YEAR || datetime->month < 1u ||
       datetime->month > 12u || datetime->day < 1u ||
       datetime->hour > 23u || datetime->minute > 59u ||
       datetime->second > 59u) {
        errno = EINVAL;
        return -1;
    }

    if(datetime->day > days_in_month(datetime->year, datetime->month)) {
        errno = EINVAL;
        return -1;
    }

    for(year = RTC_EPOCH_YEAR; year < datetime->year; ++year)
        days += leap_year(year) ? 366u : 365u;

    for(month = 1u; month < datetime->month; ++month)
        days += days_in_month(datetime->year, month);

    days += datetime->day - 1u;
    seconds = days * SECONDS_PER_DAY +
              datetime->hour * SECONDS_PER_HOUR +
              datetime->minute * SECONDS_PER_MINUTE + datetime->second;

    if(seconds > UINT32_MAX) {
        errno = ERANGE;
        return -1;
    }

    *counter = (uint32_t)seconds;
    return 0;
}

int rtc_datetime_compare(const rtc_datetime_t *lhs,
                         const rtc_datetime_t *rhs, int *result) {
    uint32_t lhs_counter, rhs_counter;

    if(!result) {
        errno = EFAULT;
        return -1;
    }

    if(rtc_datetime_to_counter(lhs, &lhs_counter) < 0 ||
       rtc_datetime_to_counter(rhs, &rhs_counter) < 0)
        return -1;

    *result = lhs_counter < rhs_counter ? -1 : lhs_counter > rhs_counter;
    return 0;
}

int rtc_get_datetime(rtc_datetime_t *datetime) {
    uint32_t counter;

    if(rtc_get_counter(&counter) < 0)
        return -1;

    return rtc_counter_to_datetime(counter, datetime);
}

int rtc_set_datetime(const rtc_datetime_t *datetime) {
    uint32_t counter;

    if(rtc_datetime_to_counter(datetime, &counter) < 0)
        return -1;

    return rtc_set_counter(counter);
}
