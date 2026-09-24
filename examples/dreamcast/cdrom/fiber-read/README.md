# Direct DMA with application-owned fibers

Requires a bootable data disc with at least eight readable cooked sectors at
the last data track's start. Build `addons/libfiber_disc` first, then this
example. It links `-lfiber_disc`; it does not start the Fiber Service Executor.

The loader child submits a direct DMA read and parks cooperatively. A sibling
keeps doing bounded work and yielding to the main fiber. The main fiber pumps
completions and schedules ready children. After request and callback retirement,
the loader checks status and releases its read handle. An ordinary direct PIO
comparison occurs only after both children finish and the adapter is destroyed.

The owner imposes a 30-second application deadline, then requests cancellation
and allows 15 seconds for draining. A failed drain halts the diagnostic rather
than freeing a live buffer or callback context. The driver itself receives a
nonzero 10-second execution timeout. These are separate deadline scopes.

`FIBER-READ: PASS` reports a matching payload; `sibling-steps` is illustrative
and timing-dependent, not a throughput benchmark. Use `fiber-disc-contract`
for deterministic proof that a sibling runs while another fiber is parked.

This example is compile/link validated separately from live-media execution.
Physical hardware is still needed for DMA/cache/timing/recovery validation.
