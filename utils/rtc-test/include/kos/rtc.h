#ifndef __RTC_TEST_KOS_RTC_H
#define __RTC_TEST_KOS_RTC_H

#include <stdint.h>

#define RTC_COUNTER_UNIX_EPOCH UINT32_C(631152000)

typedef enum rtc_weekday {
    RTC_WEEKDAY_SUNDAY = 0,
    RTC_WEEKDAY_MONDAY,
    RTC_WEEKDAY_TUESDAY,
    RTC_WEEKDAY_WEDNESDAY,
    RTC_WEEKDAY_THURSDAY,
    RTC_WEEKDAY_FRIDAY,
    RTC_WEEKDAY_SATURDAY
} rtc_weekday_t;

typedef struct rtc_datetime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
} rtc_datetime_t;

int rtc_get_counter(uint32_t *counter);
int rtc_set_counter(uint32_t counter);
int rtc_counter_to_datetime(uint32_t counter, rtc_datetime_t *datetime);
int rtc_datetime_to_counter(const rtc_datetime_t *datetime,
                            uint32_t *counter);
int rtc_datetime_compare(const rtc_datetime_t *lhs,
                         const rtc_datetime_t *rhs, int *result);
int rtc_get_datetime(rtc_datetime_t *datetime);
int rtc_set_datetime(const rtc_datetime_t *datetime);

#endif
