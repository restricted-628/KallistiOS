# Managed mono playback tests

Run `make test`; sanitizer variant:
`make test EXTRA_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`.

The fixture compiles actual port, channel reservation, playback, P1a SH-4 and ARM
observer code. It reuses the P1a fixture's fake OS, FIFO and G2 RAM, with a selectable
ARM callback result for classified rejection. The separate ARM dispatcher suite
covers real ARM validation; this is a host ownership/deadline test, not hardware.

Coverage includes all256 active priority pairs, retained sample obligations,
refusal/rejection and prior attribution, exactly-once commits, interference and
STOP recovery, ACK retry, close/attach races, stale generations, nested deadlines,
identity exhaustion and LOST retaining capacity. Test-only resets clear the fake
world between cases; they are not runtime recovery entry points. No sample
allocator/free endpoint is supplied. The optional experimental completion tests
exercise the actual ARM latch and host priority wrapper, not physical retirement.

The completion fixture covers identity across ACK/UPDATE/replacement, reuse of a
channel by a new attachment, old/foreign records, late observations, legacy and
firmware loss, header/record errors, saturation, deadlines and old capability
fallback. ARM dispatch tests separately exercise destructive LP inputs, delayed
starts, alternating channels and tick0. Register inputs are simulated; neither
host suite qualifies physical LP attribution or sample-memory reuse.

## Test timing

Protocol-only queries, polling and receipt settling use a one-second caller
budget to separate protocol checks from short host-scheduling deadlines. Stale
challenge, paused queue and lock-timeout cases retain explicit short budgets.
Failures print the case/operation, result, errno and elapsed time; there are no
automatic retries. Production timeout constants and behavior are unchanged.
