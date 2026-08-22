# Store-queue ownership and MMU safety

The SH-4 store queues are a shared CPU resource. KOS serializes their use with
a recursive mutex and preserves the destination mapping for each nested lock.
This topic adds no thread, fiber, periodic work, or permanent allocation.

## Lock ownership

`sq_lock()` may block and is unavailable from interrupt context. It now returns
`NULL` with `errno` set when the mutex cannot be acquired, the destination is
not 32-byte aligned, the fixed recursion-state capacity is exceeded, or a
recursive caller changed MMU mode during the transaction.

The default recursion-state capacity is eight. All eight entries are usable;
an attempted ninth acquisition fails with `EOVERFLOW` and leaves the outer
eight acquisitions intact.

Only the owning thread may call `sq_unlock()`. A non-owner call is diagnosed
and cannot release or reprogram another thread's transaction.

## MMU coexistence

Each recursion level records whether address translation was enabled when the
mapping was installed. Nested mapping restoration uses that recorded mode.
Changing MMU enablement between `sq_lock()` and its matching `sq_unlock()` is a
programming error because QACR mappings and the two reserved SQ TLB mappings
are not interchangeable.

The driver diagnoses such a transition on unlock. Assertion-enabled builds
stop at the violated invariant. Assertion-disabled builds release the affected
recursion level without guessing which mapping mechanism is safe to program.

The later optional fiber runtime may use this ownership boundary to reject a
cooperative transfer while its carrier thread owns the store queues. Fibers are
not part of this driver and are not required to use any SQ API.

## Checked burst helpers

`sq_cpy()` and the patterned setters validate nonzero operations before
touching hardware:

- destinations must be 32-byte aligned;
- copy sources must be at least 4-byte aligned;
- byte counts must be multiples of 32; and
- source and destination ranges must not wrap the address space.

They return `NULL` with `errno` set on validation or lock failure. A failure
between one-megabyte chunks may leave the already completed prefix visible.
Zero-length operations remain no-ops and return the original destination.

`sq_clr()` retains its legacy void interface. It stops when the underlying
setter fails and leaves the error in `errno`, but cannot return it directly.

## Validation

`examples/dreamcast/basic/sq-safety` checks malformed burst rejection, all
eight recursion slots, ninth-level overflow handling, and successful MMU-off
and MMU-on copies.

Compilation validates the interfaces and locking paths. Emulator and physical
hardware execution remain runtime gates for actual SQ write-back and MMU-mode
behavior.
