# Dreamcast RTC audit

## Scope

This audit covers the console's system real-time clock, its 32-bit persistent
counter, conversion to civil dates, cached boot-time publication, and the POSIX
clock entry points that consume it. Timer-capable Maple peripherals remain a
separate driver because their clocks have independent transport, state, and
failure behavior.

## Counter model

The hardware stores unsigned seconds since 1950-01-01 00:00:00. Its complete
range is therefore:

- counter `0`: 1950-01-01 00:00:00, Sunday;
- counter `631152000`: 1970-01-01 00:00:00, Thursday;
- counter `UINT32_MAX`: 2086-02-06 06:28:15, Wednesday.

`rtc_get_counter()` and `rtc_set_counter()` expose that native counter without
requiring callers to reinterpret it as a Unix timestamp. The existing
`rtc_unix_secs()` and `rtc_set_unix_secs()` interfaces remain available. Their
representable Unix range is exactly -631152000 through 3663815295 seconds.

The previous read path subtracted the epoch delta in unsigned arithmetic.
Dates from 1950 through 1969 consequently wrapped into large positive values
before conversion to the target's 64-bit `time_t`. The conversion now promotes
the raw counter first. The write path validates bounds before adding the epoch
delta, avoiding signed overflow for extreme `time_t` inputs.

## Calendar API

`rtc_datetime_t` is a timezone-free Gregorian representation with full year,
month, day, hour, minute, second, and derived weekday fields. The following
functions are hardware-independent except for the final get or set operation:

- `rtc_counter_to_datetime()`;
- `rtc_datetime_to_counter()`;
- `rtc_datetime_compare()`;
- `rtc_get_datetime()`;
- `rtc_set_datetime()`.

Conversion validates leap years, month lengths, field ranges, and the exact
32-bit counter endpoint. The weekday uses Sunday as zero and is derived from
the known epoch; an input weekday is ignored so stale redundant metadata cannot
contradict an otherwise valid date.

No periodic worker or application-driven server is added. KOS already derives
`CLOCK_REALTIME` from a cached boot timestamp plus its monotonic timer. Direct
RTC access is bounded, while calendar conversion is pure arithmetic. An
additional thread would reserve resources without improving either operation.

## Atomic register access

An RTC write is a multi-register protocol: open the write window, write the
low half, write the high half, verify the resulting counter, and close the
window. The older implementation locked G2 separately for each register, so a
thread switch could interleave two setters or permit a reader to observe mixed
halves.

The complete protocol now runs under one G2 lock. That lock also masks IRQs,
which prevents scheduling on the single SH-4 core until the write window is
closed and the cached boot time has been published. Verification accepts the
requested value or the following second because the clock can legitimately
advance between the write and readback. Every failure path still closes the
write window.

`dc_boot_time` remains exported for binary compatibility, but KOS accesses it
through `arch_rtc_boot_time()`. Loads and stores are IRQ-protected because
`time_t` is 64-bit while the processor's natural atomic word is 32-bit. This
prevents a reader from combining halves from two different clock settings.

## POSIX corrections

The realtime clock path now retains the signed `time_t` boot offset instead of
truncating it to `uint32_t`, so pre-1970 values remain meaningful.
`clock_settime()` validates `tv_nsec`, and `settimeofday()` validates `tv_usec`
before converting it. The persistent clock has whole-second resolution, so a
valid fractional field is truncated when the hardware is set.

`clock_getcpuclockid()` previously rejected every PID because its two supported
PID comparisons were joined with logical OR. It now accepts zero and `KOS_PID`
and rejects other processes as intended.

## Validation

`utils/rtc-test` compiles the production calendar and register-access sources
against host shims with strict C11 warnings enabled. The calendar suite checks
the epoch, Unix epoch, leap day, maximum counter, invalid dates, overflow,
comparison, and round trips. The register suite checks pre-1970 signed
conversion, null handling, Unix bounds, exact write ordering, uninterrupted G2
ownership, one-second verification advance, control closure after repeated
failure, and coherent cached boot-time publication.

The read-only `examples/dreamcast/basic/rtc` program exercises the public
counter and calendar round trip without changing the battery-backed clock.

The tests use a mocked G2 register bank and do not set an emulator or physical
console clock. Physical write timing and battery-backed persistence remain a
hardware validation gate.
