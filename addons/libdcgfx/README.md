# Graphics addon extraction workspace

`libdcgfx` is a working package name, not an installed or upstream-approved port.
This branch prepares one shared graphics library with 2D and 3D components.
It is based on integrated commit `b554f2b9e64eef3c729b8f63bd07f6d6f0b3291e`.

## Current status

- `sources.mk` explicitly identifies implementation units to move.
- `utils/addon-extraction/audit.py` checks complete, non-overlapping ownership
  against the current PVR and math build lists, and detects new direct private
  PVR dependencies in the selected files.
- The implementation is **still built into libkallisti**. There is deliberately
  no addon Makefile or `kos/dreamcast.cnf` yet: silently building duplicate
  definitions would not constitute extraction.
- A passing audit is an inventory check, not proof of standalone compatibility.

Run from the checkout root:

```sh
python3 utils/addon-extraction/audit.py
```

## Ownership

KOS retains interrupts, DMA, MMU/cache mechanisms, PVR initialization and scene
ownership, raw TA submission, VRAM allocation/reservation, texture layout and
transfer, and hardware material-packet validation. `pvr_material.c` stays;
`pvr_material_recipe.c` moves. The existing matrix/FPU ABI and `matrix3d.c` stay;
the newer camera/stack/composition helpers move with graphics initially.

The addon owns Compact formats and their validation, loading, scene/hierarchy
handling, caches, skinning/morphing, lighting, toon/wire rendering, particles,
cells/tilemaps, animation, collision, and texture-residency replacement policy.
Matching headers, host tools, tests, fixtures, documentation, and examples must
move together with the implementation in later extraction commits.

## Ordered gates before enabling the new archive

1. Remove `pvr_geometry.c` and `pvr_chunk_render.c`'s direct dependency on
   `pvr_internal.h` and `pvr_state` (including modifier-volume submission).
   Use a narrow public submission contract or a minimal core
   adapter. Do not export the driver's private state.
2. Split hardware material declarations from high-level recipe declarations.
   Define which of the newer matrix declarations need compatibility headers.
3. Move the listed sources and their graphics-private headers, update host-test
   and tool paths, and build a single archive. Remove the same objects from the
   kernel build **and its build staging directory** so stale objects cannot be
   accidentally archived into `libkallisti`.
4. Remove addon-only includes from `arch/kos.h` and addon-only kernel exports.
   Keep compatibility headers where useful; do not change Compact wire formats
   or rewrite algorithms as part of this mechanical move.
5. Make graphics consumers link explicitly. Move graphics SH4ZAM dependencies
   out of the global KOS link policy only after checking the remaining core
   header and symbol dependencies. Reuse the existing SH4ZAM kos-ports package;
   do not modify or relabel its upstream implementation.
6. Move Compact LZ4 adapters out of generic liblz4. Keep the fiber service
   adapter optional so raw asset and generic LZ4 users need no executor.
7. Build bare KOS without this addon, build the addon against the documented
   KOS prerequisites, run its host tests, and link representative 2D and 3D
   examples. Verify archive symbol ownership and test a clean build as well as
   an incremental transition. Track emulator and physical-hardware results
   separately.

After these gates, extract a source repository preserving relevant history and
provide a kos-ports recipe. The integrated KOS fork can consume a pinned version
instead of maintaining a second source copy. This branch itself is not a KOS PR.

## Related workstreams

- `master`: integrated development; unchanged by this staging branch.
- `pr/keyboard-attach-clear`: isolated keyboard state-clearing correction.
- `pr/vmu-timestamps`: isolated timestamp-encoding correction.
- `pr/maple-capability-matching`: isolated extended capability matching.

The PR branches start independently at upstream commit
`804b3195ebd1a06a27cc2b3a5eacf7a2429040a3`; they do not contain this graphics
manifest or the integrated fork's unrelated changes. Existing older topic
branches are preserved, not automatically rebased or submitted.

Fiber scheduler support needs a separate kernel API review. The executor,
codec implementations, and codec-to-service adapters form later addon tracks;
they are not prerequisites for this initial ownership inventory. Proprietary
libraries, BIOS dumps, and reverse-engineering notes must remain local and must
not enter any published branch or extracted repository history.
