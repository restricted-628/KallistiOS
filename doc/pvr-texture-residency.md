# Fixed-slot texture residency

`pvr_txr_residency_t` provides an opt-in policy layer for applications whose
complete texture set is larger than the VRAM they choose to reserve. It keeps
one homogeneous set of checked texture surfaces in a contiguous VRAM
reservation and maps application-defined 32-bit identifiers onto those slots.

The cache does not replace the PVR allocator or texture transfer APIs. It adds
only bounded residency state:

- slot and surface arrays are supplied in main RAM by the caller;
- initialization performs one contiguous `pvr_mem_malloc()` allocation;
- every slot has the exact layout of one unbound surface prototype;
- lookups, reservation, publication, pinning, and eviction allocate nothing;
- no thread, transfer queue, decompressor, source buffer, or periodic task is
  created; and
- all public operations require external serialization in ordinary thread
  context.

This makes the facility pay-for-use and leaves asset storage, compression,
prefetch prediction, and frame policy with the application.

## Loading contract

A miss is handled in two phases:

1. `pvr_txr_residency_reserve()` selects an empty slot or the oldest ready
   unpinned slot. The returned slot enters `PVR_TXR_RESIDENCY_LOADING` with one
   caller-owned pin.
2. The caller fills the standard `pvr_txr_surface_t` using a synchronous or
   asynchronous checked upload. After the transfer is terminal, it calls
   `pvr_txr_residency_publish()` on success or
   `pvr_txr_residency_abort()` on failure or cancellation.

Publication retains the original pin. The application can therefore compile a
material and submit geometry immediately without a gap in which another cache
operation could evict the newly loaded texture.

A loading identifier is not visible as ready: acquisition reports `EAGAIN`.
An aborted partial upload becomes empty and cannot be sampled. The associated
asynchronous request must be terminal before either publish or abort, because
the residency layer deliberately does not own request lifetime.

## Sampling and eviction safety

`pvr_txr_residency_acquire()` returns a generation-checked handle and increments
the slot pin count. `pvr_txr_residency_unpin()` must not run until every render
which may sample the surface has completed. A render ticket or the established
PVR completion wait can provide that boundary.

Pinned and loading slots are never eviction candidates. If every slot is in
one of those states, reservation reports `EBUSY`. Reusing an old handle after
its slot has been replaced reports `ESTALE`, preventing it from unpinning a
different texture which later occupied the same VRAM.

Recent use is recorded on successful acquisition and publication. Empty slots
are selected by index before eviction begins; full caches choose the ready,
unpinned slot with the oldest stamp. Cumulative hit, miss, eviction, loading,
and pin counts are available through `pvr_txr_residency_get_status()`.

## Integration

Every resident slot is an ordinary checked surface. While its handle remains
pinned it can be used with normal polygon contexts, compact-model texture
binding tables, compact VQ sampling addresses, synchronous transfers, or the
existing interrupt-driven asynchronous DMA requests.

The cache uses identical slot layouts intentionally. Fixed-size slices avoid
runtime VRAM fragmentation and make replacement deterministic. Applications
which need mixed texture sizes can create several residency caches for size or
format classes, or continue using individual surfaces and general contiguous
reservations.
