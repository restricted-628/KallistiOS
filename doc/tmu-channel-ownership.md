# Exclusive TMU1 ownership

This opt-in mechanism reserves TMU1 for one caller, configures a periodic
down-counter, and restores the previous stopped channel configuration on
release. TMU0 remains the scheduler timer and TMU2 remains the uptime timer.
It adds no thread, fiber, shared workqueue, graphics or SH4ZAM dependency.

## Admission and lifecycle

The driver keeps one owner pointer. Claim allocates one small control structure.
Reserved TMU0/TMU2, an existing
TMU1 claim, a running TMU1, or a pending TMU1 underflow reject admission. Claim
and release are thread-context operations. Configure/start/stop/snapshot use
short interrupt-masked critical sections; callbacks execute in IRQ context.
The callback may stop its own timer, but cannot block, allocate or release it.

The pending-underflow check is deliberate: UNF is set by hardware. Software
can clear it but cannot recreate a cleared event by writing its saved bit back.
The prior owner must handle or explicitly clear it before handing TMU1 off.
An admitted claim preserves TCOR, TCNT, TCR configuration, IRQ handler/data and
priority. Release stops owned activity, restores the saved state and frees the
control structure. It does not resume a legacy timer: admission requires it
to have been stopped.

Legacy timer mutators refuse TMU1 with EBUSY while claimed, including the void
priority-control wrappers (which set errno). Read-only legacy observations
remain available. Direct register writes and generic IRQ handler/priority
changes bypass this API; callers must coordinate them. Release requires the
owner to prevent new calls and finish other thread-context users. Released
handles must never be reused; pointer identity is not a generation token.

## Counts and timing

The new API programs a logical N-tick period as TCOR/TCNT = N - 1. Supported
divisors are 4, 16, 64, 256 and 1024. A null callback leaves interrupts masked;
expirations counts handled IRQs, not all physical wraps in polling mode.
Long masking may coalesce multiple wraps into one pending flag.

Starting an already-running channel is a no-op, preserving pending state.
Starting a stopped configured channel resumes its count and discards its old
pending flag. Reconfigure first to restart with a full period. Snapshot count
and running state are coherent against software mutation, but the hardware
counter itself continues to advance.

Conversions use the existing KOS peripheral-clock constant, round tick-to-ns
down and ns-to-tick up, and reject results exceeding UINT32_MAX ticks.
Zero duration converts to zero ticks, but a zero configured period is invalid.
Elapsed snapshots report only modulo-one-period elapsed ticks.

The existing TMU0/TMU2 reload conventions and uptime sampling algorithm are
unchanged. Legacy timer_prime now rejects speed zero and uses a wide divisor
to prevent multiplication overflow. This branch does not promise exact wall
clock calibration, change CLOCK_MONOTONIC/RTC behavior, or alter timer events.

The [Renesas SH7750 family hardware manual, section 12.2.5](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware)
documents TCR/UNF and counter behavior. No proprietary SDK material is included.

## Validation boundary

`utils/tmu-channel-test` compiles the production driver and public timer header.
Only MMIO lvalues, IRQ operations and allocator hooks are substituted. It tests
admission, failure cleanup, old-state restoration, legacy exclusion, callback
self-stop, IRQ-context release rejection, running-start idempotence, all five
divisors, overflow, and modulo elapsed counts. A host-only 128-bit oracle checks
conversion arithmetic independently. The register arrays are not a cycle-
accurate timer, do not implement hardware UNF write semantics, and do not prove
IRQ delivery latency or physical clock frequency.

The `basic/threading/tmu1-channel` target probe generates an actual pending
underflow, checks rejection, receives ten periodic callbacks, stops in the
callback, restores the previous stopped state and exercises legacy use again.
It needs exclusive access to an initially unused TMU1. Emulator and physical
hardware results must be reported separately; physical timing remains open.

### Validation snapshot (2026-09-24)

- 38,672 host checks pass GCC 14 GNU17/strict C23, Clang strict C2x and
  Clang GNU17 ASan/UBSan. Both pre-fix negative controls fail as expected.
- Narrow SH-4 GCC 16.2 SDK build, four focused driver/example -Werror builds
  and nine generated API exports pass inspection. The narrow gthr header
  required a local explicit-C compile workaround, not a source change here.
- TMU1 target probe passes interpreter/dynarec on the narrow branch, integrated
  master and both complete fiber bundles (eight runs).
- Existing shared timer-event probes pass both emulator modes on the three
  integrated trees (six runs), retaining lazy worker counts 0,1,0.
- Uptime sampling and primary scheduler-timer source compare byte-identically
  with upstream. No real-console timing, jitter or IRQ-starvation measurements
  are claimed.
