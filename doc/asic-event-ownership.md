# ASIC event ownership

KOS historically exposed ASIC event handler installation and interrupt-mask
changes as independent operations. That remains useful for simple, internal
users, but it cannot prevent two drivers from believing they own the same
event source. This topic adds an optional exclusive claim without changing the
legacy API.

## Claim lifecycle

`asic_evt_claim()` atomically verifies that an event has no handler and is not
enabled at any ASIC interrupt level, installs the handler, records its chosen
level, enables the source, and returns an opaque generation token. It allocates
no memory and creates no thread. The handler remains an ordinary
interrupt-context callback.

The token is required for `asic_evt_claim_mask()`,
`asic_evt_claim_unmask()`, and `asic_evt_release()`. Releasing a claim disables
the event at every level before removing its handler. Reclaiming the same event
produces a new generation, so a stale token cannot mask or release the new
owner's event.

Claims fail with `EBUSY` instead of taking over an existing handler or enabled
mask. This preserves hardware state established by a driver which has not yet
adopted claims.

## Interaction with existing APIs

Legacy handler installation, removal, enable, and disable calls refuse to
change an exclusively claimed event and set `errno` to `EBUSY`. Existing
unclaimed users retain their previous behavior.

Threaded handlers are also tracked explicitly. They cannot replace claims, and
claims cannot replace them. Removing a threaded handler joins and destroys its
worker. Removal from that same worker is rejected with `EDEADLK`, avoiding a
self-join. ASIC shutdown drains any threaded handlers still registered.

`asic_evt_disable_all()` is intentionally lifecycle-global: ASIC startup and
shutdown may disable every mask regardless of individual ownership. Drivers
must not treat a claim as surviving ASIC shutdown.

## Inspection and cost

`asic_evt_get_status()` returns a coherent snapshot containing enabled levels,
handler and claim presence, and a 64-bit dispatch count. Snapshot collection
briefly disables interrupts so a 32-bit SH-4 never observes a torn count.

The ownership tables are fixed kernel metadata. A claim has no heap, stack, or
periodic-work cost; threaded handlers retain their existing per-handler worker
cost only when explicitly requested.

## Validation

`utils/asic-event-test` compiles the production ASIC implementation against a
simulated register block and worker/IRQ boundary. It verifies claim admission,
masking, release, stale-token rejection, legacy exclusion, dispatch accounting,
threaded-handler self-removal protection, and shutdown cleanup.

`examples/dreamcast/basic/asic-event-claim` provides a target-side smoke test
for claim, status, mask, unmask, and release on an otherwise unused error event.
