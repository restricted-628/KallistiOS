# Contiguous PVR memory reservations

The reservation layer keeps the established PVR memory allocator as the only
owner of dynamically allocated VRAM. One `pvr_mem_reservation_t` represents one
32-byte-aligned `pvr_mem_malloc()` allocation and can expose checked slices
without maintaining a hidden suballocator or global resource registry.

## Surface planning

Applications first initialize each `pvr_txr_surface_t` without allocating it.
`pvr_txr_surface_plan_reservation()` validates every descriptor and calculates
aligned, non-overlapping offsets plus the exact total reservation size. It uses
two passes so malformed metadata, arithmetic overflow, output aliasing, an
already-bound surface, or a plan larger than physical PVR RAM leaves every
output unchanged.

After allocating the total size, `pvr_txr_surface_bind_reservation()` binds one
surface to its exact encoded byte range. Each resulting surface remains an
ordinary checked KOS surface and works with existing upload, asynchronous
transfer, readback, material, and render-target operations.

## Ownership

The reservation owns the VRAM allocation. Bound surfaces are non-owning views;
they do not free or extend into neighboring slices. Applications must clear all
borrowed surface descriptors with `pvr_txr_surface_release()` before releasing
the reservation. The API neither tracks renderer use nor moves allocations, so
the existing rule still applies: do not release or rewrite a range while the
PVR is sampling or rendering to it.

Only reservation allocation consumes resources. Planning, slice resolution,
surface binding, and release create no thread, worker, queue, callback, or
permanent workspace.
