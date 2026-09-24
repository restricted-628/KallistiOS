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

## Destination and completion contract

`sq_lock()` returns the SQ pointer for a physical destination or its direct
P1/P2 alias. Check for NULL before writing, and pair only successful locks with
`sq_unlock()`. It is a thread-context blocking operation, not an IRQ API.
The default capacity is eight recursive acquisitions; the ninth fails without
changing the active mapping. A nested unlock restores the outer mapping.
Queue contents are not saved: finish submitting data before nesting. Keep the
MMU enable state fixed throughout a transaction. General translated source or
destination resolution and MMU context retirement are separate work.

Do not substitute `SQ_MASK_DEST()` for the returned pointer in MMU-on code.
Copies/fills reacquire at each 1 MiB batch and at 64 MiB external-area boundaries.
The latter matters because QACR supplies address bits not present in the SQ
pointer when translation is disabled. This does not make nonexistent physical
regions safe to access; the caller must own valid burst-capable destinations.
Renesas documents the address construction and completion-by-store behavior in
[SH7750 hardware manual, section 4.7](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware).

`sq_wait()` uses volatile stores to both queues. It waits for submitted bursts,
not unsubmitted data, and overwrites one word in each queue. Call it while still
holding ownership when completion is needed before handing ownership away.
`sq_unlock()` is not an implicit completion fence or cache-coherency operation.
RAM destinations still require the appropriate cache maintenance by the caller.

## Validation

`utils/sq-test` compiles the production C driver with host mutex/MMU substitutes
and mapped fake register/queue memory. It checks failure paths, ownership,
recursion, mapping restoration, MMU-mode mismatch, QACR boundary batching, and
volatile completion stores. It does not simulate bus timing or real MMU/cache
behavior. Its mutex fake rejects contention instead of implementing scheduling.
No checks or memory clobbers are added inside the burst loops.

The `examples/dreamcast/basic/sq-safety` target probe exercises both MMU states,
recursive restoration, and both copy-alignment paths across the 1 MiB boundary.
The artificial 64 MiB crossing is host-only: adjacent external regions are not
interchangeable RAM on a real Dreamcast. Physical hardware remains a required
validation gate.

This extraction deliberately excludes fiber hooks, default-MMU policy, cache
mode policy, and the larger MMU/TLB retirement series. The integrated providers
retain their existing cooperative-switch ownership guard.

The caller audit adds failure handling to the direct PVR, SPU, stereo-split,
and example acquisitions. The follow-up stereo-split mapping, sample-order,
and tail corrections are described in [stereo SQ uploads](stereo-sq-upload.md).

Recorded validation (2026-09-24): 2,097,250 host checks pass with GCC 14
GNU17/strict C23, Clang GNU17/C2x, and Clang ASan/UBSan. Replacing the boundary
calculation with the old fixed-size batch makes the boundary regression fail.
SH-4 GCC 16.2 kernel/probe builds and Flycast interpreter/dynarec runs pass
on the narrow and integrated trees. These are not physical-hardware results.
Modified zclip/YUV example objects compile; the Parallax example cannot be
compiled locally without its external `plx/matrix.h` dependency.
