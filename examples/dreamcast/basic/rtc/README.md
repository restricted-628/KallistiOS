# RTC counter and calendar example

This read-only example retrieves the console RTC's complete 1950-epoch counter,
converts it to `rtc_datetime_t`, prints the corresponding signed Unix time, and
verifies that converting the calendar value back produces the same counter.

The example deliberately does not call `rtc_set_counter()`,
`rtc_set_datetime()`, `rtc_set_unix_secs()`, `clock_settime()`, or
`settimeofday()`. Those functions alter the battery-backed system clock and
should only be used after the application has obtained an intentional date and
time from the user or another trusted source.

Build with `make`, or use `make run` with the configured KOS loader.
