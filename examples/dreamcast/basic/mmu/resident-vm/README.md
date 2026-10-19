# Resident VM target probe

Build with the assigned KOS environment and `make`. This uses the core fiber
provider; it does not link the exclusive addon or certify that provider.
P1 code accesses three resident P0 data pages spanning a page-table boundary.
Two ordinary fibers share the selected context and direct stacks; another
carrier accesses the same data while the child runs without yielding. Users
are joined/destroyed before decommit/release. Probe code checks zero/recommit,
protection transitions, OCRAM rejection, copied queries and masked-call rejection.

A separate low-level mapping preflight loads its own page into replaceable UTLB
slot 0. This lets runtimes with on-demand MMU activation observe a non-SQ LDTLB.
The preflight then replaces that mapping with different backing and checks the
new value. A stale translation fails before VM admission. The checked unmap
retires the seed before freeing its backing; subsequent VM pages are not
preloaded and must use normal TLB refill. A result with `translation=0`
and `data=SKIPPED` is structural coverage only. Full translated execution needs
`failures=0 translation=1`, nonzero preemption samples for both children and
`data=EXECUTED`. Classify the printed result, not just emulator exit status.

Read-only protection is checked by allowed reads and state changes. The probe
does not attempt a prohibited write or invent a recovery handler; actual fault
delivery and physical cache/alias behavior remain separate hardware gates.
The emulator can expose different behavior in interpreter and dynarec modes.
