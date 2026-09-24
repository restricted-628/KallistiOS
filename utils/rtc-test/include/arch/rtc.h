#ifndef __RTC_TEST_ARCH_RTC_H
#define __RTC_TEST_ARCH_RTC_H

#include <stdint.h>
#include <time.h>

#define RTC_COUNTER_UNIX_EPOCH UINT32_C(631152000)

extern time_t dc_boot_time;

int rtc_get_counter(uint32_t *counter);
int rtc_set_counter(uint32_t counter);
int arch_rtc_get_counter(uint32_t *counter);
int arch_rtc_set_counter(uint32_t counter);
time_t arch_rtc_unix_secs(void);
int arch_rtc_set_unix_secs(time_t time);
time_t arch_rtc_boot_time(void);
int arch_rtc_init(void);

#endif
