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

1. A bounded tile-map compiler over existing cell/sprite sinks, with viewport
   clipping, wrap/clamp policies, and transformed maps.
2. Compound trilinear and bump-material recipes, including secondary color
   and alpha accumulation and explicit draw ordering.
3. Extended multipass depth preserve/clear policy, followed by a portal or
   mirror fixture; keep color retention independent from depth.
4. Model-asset roles for emissive/unlit, lightmaps, environment mapping and
   bump inputs, using existing texture converters and prepared bindings.
5. Target numerical/ABI and performance fixtures for SH4ZAM consumers, with
   explicit error tolerances, XMTRX preservation, and warm/cold measurements.
6. Physical image tests for translucent accumulation, modifier clipping and
   presort, compact VQ, global texture state, RTT visibility, and DMA/SQ use.

These are distinct deliverables, not newly completed features. The existing
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
