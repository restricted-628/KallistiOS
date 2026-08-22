# Maple clock status

This read-only example locates a Maple clock device, validates civil-date edge
cases, submits an asynchronous clock read, and verifies the coherent completion
snapshot and callback. It then compares the result with the synchronous civil
time API.

No clock value is written. Applications that need to set a peripheral clock can
use `vmu_clock_set_time()` or `vmu_clock_set_time_async()` with a validated
`vmu_clock_time_t` value.

The program prints `VMU-CLOCK: PASS` and exits when all checks pass.
