# Expansion, G2, and external-interrupt audit

## Scope

This audit covers the Dreamcast expansion connector, shared G2 PIO and DMA
facilities, ASIC external-event ownership, and discovery of the built-in KOS
Ethernet and modem drivers. It does not add a second driver manager or an
application pump.

## Existing KOS baseline

KOS already provided width-specific G2 PIO access, four DMA channels, ASIC
event handlers, and complete device drivers for the PCI Ethernet adapter,
8-bit Ethernet adapter, and modem. Network-driver registration also already
provided automatic selection when networking was explicitly enabled.

The missing layer was coherent ownership and inspection. Event installation
could silently replace another handler, DMA state was not observable, and no
single bounded call could report expansion capabilities without initializing
the network stack.

## Correctness repairs

### G2 PIO lock restoration

The former G2 lock suspended channels 0 through 2, omitted channel 3, and
unconditionally resumed every channel on unlock. It could therefore resume a
transfer that its owner had intentionally left suspended.

`g2_ctx_t` now records all four suspend-register states. `g2_lock()` suspends
all four channels while the FIFO drains, and `g2_unlock()` restores each exact
entry state.

### DMA validation and lifecycle

The legacy DMA submission documented a 32-byte length requirement but rounded
an invalid length upward. That could transfer beyond the caller's allocation.
Submissions now reject zero, misaligned, oversized, invalid-direction, and
blocking-from-interrupt requests. SH-4 endpoints must resolve to the available
main-RAM range instead of being silently masked into an unrelated address.
Cacheable sources are written back before DMA, and cacheable destinations are
invalidated both before submission and after the engine becomes terminal.
With the MMU enabled, only direct P1/P2 aliases are accepted; translated P0/P3
mappings are rejected instead of being mistaken for contiguous physical RAM.

Additive channel operations provide:

- coherent state, byte count, sequence, completion, cancellation, and callback
  snapshots;
- one bounded or indefinite thread-context waiter;
- explicit suspend and resume;
- cancellation that disables hardware, suppresses the callback, and wakes the
  waiter;
- shutdown cancellation before channel teardown.

Completion callbacks remain in interrupt context. They can chain another DMA
operation without the prior completion clearing the new operation's callback
state.

### External-event ownership

`asic_evt_claim()` installs and enables an event as one exclusive operation and
returns a generation-checked token. Only that token can mask, unmask, or
release the event. Claims use fixed driver state and allocate no memory.

The four G2 DMA completion events and the BBA, LAN-adapter, and modem external
interrupts now use claims. A second driver receives `EBUSY` instead of silently
replacing the first handler. Event status snapshots expose enabled IRQ levels,
claim state, handler presence, and dispatch counts.

Threaded ASIC handlers now reject duplicate installation and are destroyed
during ASIC shutdown. Explicit modem shutdown now releases its interrupt claim
and timer immediately; previously both survived until process exit.

## Expansion discovery

`expansion_probe()` returns a typed capability snapshot without starting the
network stack, allocating memory, or creating a thread.

The default probe is side-effect-free. It reads the PCI bridge signature and
observes current external-interrupt ownership. An inactive 8-bit device cannot
be identified safely this way, so the result explicitly reports an incomplete
probe.

`EXPANSION_PROBE_RESET_8BIT` opts into the existing bounded LAN identification
and modem self-test. This mode resets the inactive 8-bit interface, can take
several hundred milliseconds, and is rejected while any 8-bit interrupt owner
is active.

The snapshot reports device class, interface and network capabilities, known
maximum line rate, active ownership, completeness, and whether a reset was
performed. Unrecognized active PCI or 8-bit owners remain visible without
being mislabeled as a known device.

## Resource proportionality

All new state is fixed bookkeeping attached to four DMA channels and the ASIC
event table. Expansion probes execute only when called. No worker, polling
thread, cache, or persistent probe buffer is added.

## Validation

The software validation set consists of:

- strict cross-compilation of the affected hardware and device drivers;
- a complete clean KOS build and generated-export verification;
- an expansion-probe example with safe and explicit reset modes;
- source-format, provenance, and static-analysis checks.

The default probe is suitable for emulator integration. Physical validation is
still required for DMA suspend timing, cancellation during bus latency,
external-event electrical behavior, PCI bridge variants, 8-bit reset behavior,
and all modem or Ethernet hardware variants.
