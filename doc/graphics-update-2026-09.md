# Graphics integration checkpoint: 2026-09-08

This checkpoint follows sound commit `72c57216`. It integrates official KOS
master through `33c6e0ba` without replacing the cumulative graphics, sound,
or low-level driver work.

## Upstream synchronization

Five official commits were merged: C11 atomic flags, stack-protector module
exports, generated-export cleanup, expired reader/writer semaphore deadlines,
and optional GDB support in the toolchain Docker image. The merge required no
manual conflict resolution. The Docker change does not rebuild or change the
installed compilers.

SH4ZAM upstream remains at `ad353dc2cea596a7c8c7b56b05cbe2e07b84ed4a`.
There is no newer algorithm to import at this checkpoint. The bundle retains
its GCC 16 FFT operand fix, documentation fixes, and source formatting.
See `addons/libsh4zam/README.md`, `source-lock.json`, and
`local-changes.patch` for the pinned source and local delta.

## Checked material correction

The material compiler previously checked filter and list enums independently,
admitting punch-through plus either trilinear phase. The shared texture
validator now rejects that combination and trilinear without mipmaps. It also
requires square, twiddled/VQ mipmapped textures, matching the existing checked
texture-layout contract. Palette-bank bits remain distinct from layout bits;
disabled texture fields remain ignored.

These rules apply to ordinary polygons, sprites, and each two-volume texture
state. Existing asset bindings that compile through these APIs inherit them.
Failed admission leaves the destination unchanged. Raw header encoders retain
their encoding behavior. Validation does not certify that a collection of
individually valid headers forms a correct accumulation recipe.

## Remaining graphics agenda

1. Completed on September 9: a bounded tile-map compiler over existing cell
   geometry, with viewport clipping, wrap/clamp policies, and transformed maps.
   See [scrolling tile maps](pvr-tilemaps.md) and the checkpoint below.
2. Initial bounded compound trilinear and bump-material recipes landed on
   September 10, including secondary RGBA accumulation and explicit ordering.
   Physical composition/order validation and broader combinations remain open;
   see [compound material recipes](pvr-material-recipes.md).
3. Explicit multipass depth preserve/clear policy implemented September 10,
   independently of color retention. Region-array checks pass; the Vulkan
   emulator fails the clear-specific image checks. Physical depth validation
   and a portal or mirror fixture remain open.
4. Model-asset roles for emissive/unlit, lightmaps, environment mapping and
   bump inputs, using existing texture converters and prepared bindings.
5. Target numerical/ABI and performance fixtures for SH4ZAM consumers, with
   explicit error tolerances, XMTRX preservation, and warm/cold measurements.
6. Physical image tests for translucent accumulation, modifier clipping and
   presort, compact VQ, global texture state, RTT visibility, and DMA/SQ use.

The remaining parts of items 2-6 are distinct deliverables, not completed
features. The existing
animation, deformation, cells, particles, compact-model caches, and math
bridges remain the basis for them. General scene ownership and game-specific
policies belong above the current runtime.

## Execution contract clarification

The fiber math-context option saves XMTRX, not a complete per-fiber FPSCR,
FPUL or TLS environment. Temporary FPU mode and exception-enable changes must
be restored before a cooperative transfer. This clarification adds no switch
work or allocation and does not change the default lightweight fiber mode.

## Validation at this checkpoint

- Clean GCC 16.2.0 SH-4 build and ARM sound-firmware build: passed.
- All 54 host suites passed in each of four lanes: GCC 14 GNU17, GCC 14
  strict C23, Apple Clang 16 GNU17, and Apple Clang 16 strict C2x.
- Focused material validation with AddressSanitizer and UndefinedBehaviorSanitizer:
  passed, including the list/filter/mipmap matrix and unchanged-output checks.
- SH4ZAM source-lock verification: all 54 vendored files matched both the
  bundled hashes and the pinned upstream revision.
- Target C++17 and C++23 math-bridge probes compiled and linked; compiler and
  linker traces selected the bundled headers and bundled archive.
- Both SH4ZAM examples rebuilt. The integration example printed PASS in
  Flycast with explicit interpreter and dynarec configurations, covering
  camera, frustum, geometry, compact-model emission, and fiber matrix state.
- The rebuilt KOS archive contains the new atomic-flag helpers, and its
  module-export archive contains the stack-protector exports.

These runs establish build and fixture correctness. They do not establish
physical rendering conformance, instruction-wide numerical bounds, FFT
accuracy, or performance improvements. Those remain in the agenda above.

## September 9: scrolling tile maps

Added `pvr_tilemap_measure()` and `pvr_tilemap_compile()`. Both operate on
caller-owned arrays with an explicit candidate-work limit. The compiler
reuses colored cell expansion, SH4ZAM target trigonometry, frustum clipping,
and canonical geometry sinks. It introduces no allocation, runtime service,
retained scene owner, or new texture-management policy. Atlas flips, padded
rows, empty/hidden cells, independent finite/wrap/clamp axes, and transformed
views retain explicit material/list/priority routing metadata.

Validation for this addition:

- Tile-map, cell, and geometry/frustum host suites passed under GCC 14 GNU17,
  GCC 14 strict C23, Apple Clang GNU17, and Apple Clang strict C2x (12 runs).
- The tile-map suite passed AddressSanitizer and UndefinedBehaviorSanitizer,
  with rasterized coverage, interpolation, exact-capacity, and failure-atomic
  admission checks. The full unrelated host suite was not rerun for this item.
- Incremental KOS GCC 16.2.0 build and both new SH-4 ELF links passed. Both
  public functions were confirmed in the kernel and module-export archives.
- The procedural example completed all four address-policy phases and PVR
  fault checks in Flycast interpreter and dynarec modes. Its transformed
  clipped image was inspected. Target numerical checks also passed in both
  modes, including preservation of a nonidentity XMTRX across measure/compile.
- Focused Doxygen generation includes the new group and both public APIs,
  with no warning attributed to the new header. Full-tree generation was
  stopped after a prolonged run; existing unrelated group warnings remain.

The example is a correctness fixture, not a performance benchmark. Physical
console rasterization and timing remain open. Compound material recipes are
the next implementation item; no sound or driver changes are part of this one.

## September 10: compound material profiles

Added checked trilinear and bump recipe compilers to the existing material
layer. Four initial profiles cover opaque and translucent surfaces through
two or three ordered headers, with role-specific vertex contracts. They add
no allocation, scene owner, service or alternative geometry/math pipeline.
Inputs and failed outputs are preserved, including rejected aliasing. The
legacy depth-write-disable bit and secondary-buffer selectors now have
explicit comments; their ABI and raw encoding are unchanged.

Validation for this addition:

- Existing material and new recipe suites passed GCC 14 GNU17/strict C23 and
  Apple Clang 16 GNU17/strict C2x (eight focused runs). The new suite also
  passed AddressSanitizer and UndefinedBehaviorSanitizer. Unrelated host
  suites were not rerun for this item.
- The incremental GCC 16.2.0 KOS build and both new ELF links passed. The
  recipe fixture checks the actual target packet encoder, not only its host
  test double; it passed in Flycast interpreter and dynarec configurations.
- The procedural example's numeric RGB565 framebuffer check passed all seven
  samples in the explicitly labeled autosort diagnostic: four surface colors,
  the background, and two depth-occluding bars. This checks the emulator's
  secondary resolver and the recipe arithmetic on isolated geometry.
- The required presort configuration failed exactly the two translucent
  samples (white resolve polygons); the other five samples passed. Flycast's
  presorted TR route bypasses its secondary-buffer resolver. This known
  failure is retained, not hidden by weakening the recipe ordering contract.
- Both APIs were confirmed in the kernel and module-export archives and in
  the focused Doxygen material group. The focused run's only warning was the
  existing parent group omitted from that single-header input.

Physical-console composition and ordering are still required, especially for
overlapping surfaces. Fog-aware recipes, sprites, two-volume combinations,
and combined bump/trilinear are outside these initial profiles. Multipass
depth policy remains the next implementation item; no sound or driver changes
are part of this checkpoint.

## September 10: explicit multipass depth boundaries

Added `pvr_init_multipass_depth()` and the CLEAR/PRESERVE policy enum. It shares
the existing initializer, TA continuation and region layout rather than
introducing a second pass scheduler. The original initializer still clears
pass zero and preserves depth thereafter. Public configuration structures
retain their ABI; the internal clear flag fits existing structure padding.
Color retention, list routing and parameter/overflow cursors are unchanged.

Validation:

- Expanded host region tests enumerate every depth mask for one through eight
  passes over multiple rows and columns, comparing every word with the legacy
  baseline. GCC 14 GNU17/strict C23 and Apple Clang GNU17/strict C2x passed.
  AddressSanitizer/UndefinedBehaviorSanitizer passed separately.
- Incremental GCC 16.2.0 KOS build and new SH-4 example link passed. Kernel
  and module-export archives contain the new API. Focused Doxygen includes
  the function and enum in `pvr_init`; only missing parent/related groups
  outside the focused input produce warnings.
- In Flycast Vulkan, interpreter and dynarec runs each completed all nine
  direct/DMA/hybrid and legacy/preserve/clear combinations. Invalid-policy
  sentinel checks, pipeline fault checks and all submitted tile-control words
  passed. The six legacy/preserve cases pass all image samples. The three
  clear cases each fail the center sample: 3 mismatches out of 45 overall.
  The example reports failure; no expected color was weakened to obtain PASS.
  An additional OpenGL 4.1 dynarec run gives the same 3/45 mismatches.

The emulator discrepancy and source evidence are recorded in the
[multipass design](pvr-multipass-design.md#depth-boundary-validation).
This closes the driver API/encoding item, not the physical depth-clear image
gate or the portal/mirror integration fixture. No sound changes are included.
