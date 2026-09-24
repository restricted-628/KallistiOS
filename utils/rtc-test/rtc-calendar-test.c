/* KallistiOS ##version##

   rtc-calendar-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos/rtc.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

static int failures;
static uint32_t hardware_counter;

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

int rtc_get_counter(uint32_t *counter) {
    if(!counter) {
        errno = EFAULT;
        return -1;
    }

    *counter = hardware_counter;
    return 0;
}

int rtc_set_counter(uint32_t counter) {
    hardware_counter = counter;
    return 0;
}

static void check_datetime(uint32_t counter, uint16_t year, uint8_t month,
                           uint8_t day, uint8_t hour, uint8_t minute,
                           uint8_t second, uint8_t weekday) {
    rtc_datetime_t datetime;
    uint32_t round_trip;

    CHECK(rtc_counter_to_datetime(counter, &datetime) == 0);
    CHECK(datetime.year == year);
    CHECK(datetime.month == month);
    CHECK(datetime.day == day);
    CHECK(datetime.hour == hour);
    CHECK(datetime.minute == minute);
    CHECK(datetime.second == second);
    CHECK(datetime.weekday == weekday);
    CHECK(rtc_datetime_to_counter(&datetime, &round_trip) == 0);
    CHECK(round_trip == counter);
}

static void test_boundaries(void) {
    check_datetime(0, 1950, 1, 1, 0, 0, 0, RTC_WEEKDAY_SUNDAY);
    check_datetime(RTC_COUNTER_UNIX_EPOCH, 1970, 1, 1, 0, 0, 0,
                   RTC_WEEKDAY_THURSDAY);
    check_datetime(UINT32_C(1582934400), 2000, 2, 29, 0, 0, 0,
                   RTC_WEEKDAY_TUESDAY);
    check_datetime(UINT32_MAX, 2086, 2, 6, 6, 28, 15,
                   RTC_WEEKDAY_WEDNESDAY);
}

static void test_validation(void) {
    rtc_datetime_t datetime = { 2001, 2, 29, 0, 0, 0, 0 };
    uint32_t counter;

    errno = 0;
    CHECK(rtc_datetime_to_counter(&datetime, &counter) < 0);
    CHECK(errno == EINVAL);

    datetime.year = 2000;
    datetime.month = 13;
    errno = 0;
    CHECK(rtc_datetime_to_counter(&datetime, &counter) < 0);
    CHECK(errno == EINVAL);

    datetime.year = 2086;
    datetime.month = 2;
    datetime.day = 6;
    datetime.hour = 6;
    datetime.minute = 28;
    datetime.second = 16;
    errno = 0;
    CHECK(rtc_datetime_to_counter(&datetime, &counter) < 0);
    CHECK(errno == ERANGE);

    errno = 0;
    CHECK(rtc_counter_to_datetime(0, NULL) < 0);
    CHECK(errno == EFAULT);
    errno = 0;
    CHECK(rtc_datetime_to_counter(NULL, &counter) < 0);
    CHECK(errno == EFAULT);
}

static void test_compare_and_hardware_wrappers(void) {
    rtc_datetime_t earlier = { 1999, 12, 31, 23, 59, 59, 0 };
    rtc_datetime_t later = { 2000, 1, 1, 0, 0, 0, 0 };
    rtc_datetime_t fetched;
    int result;

    CHECK(rtc_datetime_compare(&earlier, &later, &result) == 0);
    CHECK(result == -1);
    CHECK(rtc_datetime_compare(&later, &earlier, &result) == 0);
    CHECK(result == 1);
    CHECK(rtc_datetime_compare(&later, &later, &result) == 0);
    CHECK(result == 0);

    CHECK(rtc_set_datetime(&later) == 0);
    CHECK(rtc_get_datetime(&fetched) == 0);
    CHECK(fetched.year == later.year && fetched.month == later.month &&
          fetched.day == later.day && fetched.hour == later.hour &&
          fetched.minute == later.minute && fetched.second == later.second);
}

int main(void) {
    test_boundaries();
    test_validation();
    test_compare_and_hardware_wrappers();

    if(failures) {
        fprintf(stderr, "%d RTC calendar test(s) failed\n", failures);
        return 1;
    }

    puts("RTC calendar tests passed");
    return 0;
}
