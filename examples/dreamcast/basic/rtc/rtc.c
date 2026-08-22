/* KallistiOS ##version##

   rtc.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static const char *const weekday_names[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

int main(void) {
    rtc_datetime_t datetime;
    uint32_t counter, round_trip;
    time_t unix_time;

    printf("KallistiOS ##version##\n\n");

    if(rtc_get_counter(&counter) < 0) {
        perror("rtc_get_counter");
        return 1;
    }

    if(rtc_counter_to_datetime(counter, &datetime) < 0) {
        perror("rtc_counter_to_datetime");
        return 1;
    }

    if(rtc_datetime_to_counter(&datetime, &round_trip) < 0) {
        perror("rtc_datetime_to_counter");
        return 1;
    }

    unix_time = (time_t)counter - RTC_COUNTER_UNIX_EPOCH;
    printf("RTC counter: %" PRIu32 "\n", counter);
    printf("Civil time:  %04" PRIu16 "-%02" PRIu8 "-%02" PRIu8
           " %02" PRIu8 ":%02" PRIu8 ":%02" PRIu8 " (%s)\n",
           datetime.year, datetime.month, datetime.day, datetime.hour,
           datetime.minute, datetime.second,
           weekday_names[datetime.weekday]);
    printf("Unix time:   %" PRIdMAX "\n", (intmax_t)unix_time);
    printf("Round trip:  %s\n", round_trip == counter ? "valid" : "FAILED");

    return round_trip == counter ? 0 : 1;
}
