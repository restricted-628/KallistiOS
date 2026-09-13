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
   remains open. A bounded rectangular portal fixture is now implemented;
   its disjoint-coverage route passes emulator checks, while its strict
   depth-clear route retains the same image-validation gate.
4. Model-asset roles for emissive/unlit, lightmaps, environment mapping and
   bump inputs, using existing texture converters and prepared bindings.
   Checked reusable context resolution now connects distinct texture inputs
   to existing bump/trilinear recipes. Bounded lightmap/emissive composition is
   now implemented too. Authored unlit metadata/import is now implemented;
   auxiliary runtime layer descriptors and preparation are implemented, while
   serialized per-material texture roles/import remain open;
   see the September 10 entries below.
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

## September 10: rectangular portal integration

Added the [portal fixture](../examples/dreamcast/pvr/portal/) without a public
scene-owner or portal API. It reuses homogeneous frustum clipping, the SH4ZAM
target transform path, checked polygon materials, canonical geometry sinks,
and direct/DMA/hybrid multipass registration. No production library code or
allocation policy changes are needed for this integration.

Two separate routes have an identical intended image. The disjoint-coverage
route leaves a real hole in the first view's wall and preserves depth. The
strict depth-clear route initially covers the opening, clears depth before
the second view, and replays a foreground occluder whose depth was lost.
Both clip remote geometry to the opening and preserve the final pass's depth.
There is no silent emulator fallback between the routes.

The shared host/target builder is checked by software rasterized full-frame
goldens, analytic UV/depth checks and deliberate missing-clear/missing-replay
negative controls. This closes the bounded portal integration fixture, not
arbitrary portal traversal, mirror rendering, physical depth-clear validation
or a scene graph. Model-asset material roles and SH4ZAM numerical/performance
fixtures remain next in the graphics agenda.

Validation for this fixture:

- Portal and existing geometry/frustum suites passed GCC 14 GNU17/strict C23
  and Apple Clang GNU17/strict C2x (eight focused runs). The portal suite also
  passed AddressSanitizer/UndefinedBehaviorSanitizer. Its aperture is not
  tile-aligned, and both analytic full-frame goldens retain exact coverage.
- GCC 16.2.0 compiled and linked the example against the current KOS build.
  No kernel/public API changes were made and no exports were needed.
- Flycast Vulkan interpreter and dynarec runs preserve the nonidentity XMTRX
  across all geometry
  preparation calls, complete direct/DMA/hybrid submission without pipeline
  faults, and pass all 27 disjoint-coverage pixel samples. The strict clear
  route fails the two remote-object samples per mode (6/27), as in the
  preceding depth-boundary test. The final result deliberately remains FAIL.
- Unrelated host suites and physical-console tests were not run for this
  fixture. It is not a performance benchmark or a general scene graph.

## September 10: Compact resource inputs for material recipes

Added `pvr_chunk_material_resolve_context()` to expose the checked polygon
context and compile flags through the existing resource resolver. One shared
mapping/validation path serves both APIs. Ordinary draws still compile one
header, with no additional output-context copy; the opt-in API adds no
allocation, texture ownership, worker, format revision or existing ABI change.

The compound-material example now resolves color and bump identifiers from
one caller-owned texture table before invoking the existing recipe compilers.
All four opaque/translucent bump/trilinear recipes compare their actual TA
headers against an independent explicit-context construction. The nonmipmapped
bump reference explicitly uses normal mip bias, matching the resolver's
established normalization of that inactive field.

Validation:

- Binding and material-recipe host suites passed GCC 14 GNU17/strict C23 and
  Apple Clang GNU17/strict C2x. Both passed AddressSanitizer and
  UndefinedBehaviorSanitizer. Checks include unchanged outputs on validation
  and compiler failure, distinct texture inputs, compact-VQ address bias,
  supersampling and two-volume state. The host binding compiler is a test
  double; real packet equivalence is checked separately on the target.
- GCC 16.2.0 rebuilt KOS and linked the example. The API is present in kernel
  and module-export archives. Focused Doxygen places the API and type in the
  resource-binding group (only the omitted parent group warns).
- Flycast Vulkan interpreter and dynarec both passed all four exact recipe
  packet comparisons and reached image validation with no pipeline fault.
  Both retained three of seven strict pixel mismatches: opaque trilinear red
  is 213 rather than 204 (tolerance eight), and both translucent centers are
  white. The image assertion remains a failure, not a submission PASS.
  Rebuilding the prior example from `ab4343b5` and running it with the same
  dynarec/settings produced exactly the same seven pixel values. These image
  failures are not introduced by resource resolution. No physical tests ran.

This closes the reusable resource-to-recipe bridge, not all material roles.
Per-material authored role metadata, lightmap/emissive composition, SH4ZAM
numerical/performance fixtures and physical rendering validation remain open.
Existing unlit and environment-map vertex policies are reused, not replaced.

## September 10: lightmap and emissive composition

Added `pvr_material_compile_lightmap()` and
`pvr_material_compile_emissive()` over the established checked context,
secondary-buffer, and canonical-geometry recipe path. Compact resource
contexts feed both directly, with separate sampling flags for each input.
The existing three-step recipe layout and previous role values are unchanged.
No model format, asset importer, startup allocation, service or global state
is added. The compiled recipe object (including the older bump/trilinear
functions) reports 2,108 bytes in its text bucket and zero data/BSS with the
current GCC 16.2.0 build; this is not a performance benchmark.

Lightmaps multiply RGB with neutral alpha one. Emission adds unlit RGB with
neutral alpha zero and saturates before the final surface blend. Neither
operation changes surface opacity or repeats lighting. The caller supplies
layer UVs/tint, matching coverage/depth and explicit ordering; unsupported
compound combinations remain errors rather than approximations.

Validation:

- Material and recipe suites passed GCC 14 GNU17/strict C23 and Apple Clang
  GNU17/strict C2x (eight runs). Recipe ASan/UBSan passed. Tests cover both
  input masks, opacity/intensity endpoints and intermediates, RGB saturation,
  preserved alpha, two input rejection paths, aliases and unchanged outputs.
  Compact binding regressions also passed GCC 14 strict C23 and Clang GNU17.
- KOS, the SH-4 recipe test and both example modes built with GCC 16.2.0.
  Both APIs are present in kernel/module-export archives. Focused Doxygen
  groups both APIs correctly; only the omitted parent group warns.
- The recipe contract test uses the real header encoder on SH-4 and passed
  under Flycast interpreter and dynarec. The layered example also passes all
  four resource-versus-explicit TA packet comparisons in both modes.
- Presorted Vulkan rendering in both modes passes the backdrop, opaque
  lightmap, opaque emission and both occluder samples. Both translucent samples
  are white rather than the expected composed colors: two of seven image
  checks fail. The strict assertion is retained. This matches the recorded
  secondary-resolve limitation; physical rendering validation remains open.
- A separately labeled Vulkan per-pixel autosort diagnostic passed all seven
  framebuffer samples, including partial-opacity layer composition with
  nonidentity texture alpha. It supports arithmetic behavior on separated
  quads, not presort ordering or physical-console conformance. No production
  setting or expected pixel was weakened to obtain that diagnostic PASS.

The bounded composition primitives are now present. Authored per-material role
metadata/import, broader compound profiles and the SH4ZAM numerical/performance
fixtures are still separate work. This does not claim complete graphics parity.

## September 12: authored unlit materials

The first authored material role now travels from glTF import through Compact
streams, ordinary/two-volume prepared admission, portable cooked caches and
standard resource/policy bindings. `PVR_CHUNK_STRIP_UNLIT` uses the formerly
reserved strip bit `0x80`; no model/cache structures or container layouts grow.
Existing flags retain their meaning. Newly flagged content requires the updated
runtime because older checked renderers reject that reserved bit.

Standard policy bindings bypass ambient/diffuse/specular and depth-cue
evaluation for an authored unlit strip even in a lit scene. They retain decoded
base color/alpha and optional vertex intensity, and clear offset color before
the optional custom callback. Checked header resolution also disables specular
without changing the caller's base context. Environment UV generation remains
independent and still requires its normal inputs. `IGNORE_LIGHT` is unchanged:
it does not mean unlit, because ambient evaluation remains independent.

The converter accepts optional or required `KHR_materials_unlit`. Base-color
factor, vertex colors, base-color textures, supported alpha modes and culling
use the existing conversion path; unused core PBR fallback lighting inputs are
ignored for this role. Unsupported extension combinations still fail. A mixed
asset test confirms the next lit material and compiled base-color image remain
unchanged. No extra material manager, worker, allocation, startup work or shader
interface is introduced. Existing quantization/color-space limits remain;
this is not a claim of exact glTF visual reproduction.

Validation includes fixed expected color/alpha values, missing-normal behavior,
environment-map rejection, specular suppression, list routing, raw-to-prepared
flag preservation, cooked serialization/reopening/materialization, optional
and required extension import, and failed-import output preservation.
Binding, render and cache host suites pass GCC 14 GNU17/strict C23 and Apple
Clang GNU17/strict C2x; ASan/UBSan runs pass for all three. Converter goldens
pass Clang GNU17 and both compilers' strict language lanes. The strict Clang
converter build uses the existing `HOST_LZ4_WARNINGS` exception for the bundled
LZ4 constant-logical-operand warning; no vendor source or test expectation was
changed to silence it.

The cache suite also exposed an older target-fixture error: its host-only
submission stub accepted current-list writes without a scene, whereas real
KOS rejects those writes with `EPERM`. The target branch now checks that exact
rejection and zero emitted vertices; the host still checks successful stub
submission. No production submission check was weakened.

The target cache suite passes Flycast interpreter and dynarec with GCC 16.2.0,
and the full KOS build passes. These are parser/cache/numerical checks, not a
physical-console or framebuffer proof of authored-material appearance.
Auxiliary texture-role metadata/import, broader compound profiles and the
SH4ZAM numerical/ABI/performance fixtures remain next; authored unlit does not
implicitly create a multipass recipe.

## September 12: auxiliary layer preparation bridge

`pvr_chunk_material_layer_t` now carries caller-owned texture/sampler state,
an explicit lightmap/emission role, RGB tint and two affine UV rows. It is
runtime metadata, not a serialized asset record and not an allocation added
to every model or strip. Applications associate it with their selected draw.

`pvr_chunk_material_resolve_layer()` reuses a previously resolved Compact
surface and the existing texture table, then calls the checked layer recipe
compiler. Sampling and mip bias remain independent across inputs. The
matching vertex helper preserves command/position/depth exactly while mapping
the caller-supplied UV set and replacing already-lit colors with the layer's
unlit tint and neutral alpha. Invalid metadata, missing textures, compilation
failures and mapped UV overflow preserve output. No allocation, resource pin,
scene owner or worker is introduced. Both resources must stay alive through
render completion; existing stream manifests do not implicitly pin auxiliary
textures supplied out of band.

Validation:

- Binding tests link the real recipe planner and pass GCC 14 GNU17/strict C23
  and Clang GNU17/strict C2x. ASan/UBSan passes. Cases cover both roles and OP/TR
  routing, independent sampling, fixed expected tint/alpha/UV results, in-place
  preparation, invalid roles/tints, nonfinite/overflowed UVs, missing texture,
  unsupported filtering, and unchanged outputs on compiler rejection.
- Full KOS and both material-example modes build with GCC 16.2.0. Kernel and
  module-export archives contain both APIs. Focused Doxygen groups them with
  the Compact bindings; only the omitted parent group warns.
- The layered example compares all four recipes with independent explicit
  contexts and checks nonidentity UV mapping plus position/depth preservation.
  It passes packet and submission checks in Flycast interpreter and dynarec.
  Constant textures isolate composition, not varying-texture interpolation;
  these runs do not close the existing presort/physical image-validation gates.

This closes the runtime preparation side, not asset import. Next is a checked
serialized material-to-layer association, independent UV-set handling and
resource/loader integration before accepting auxiliary glTF materials. The
existing PRT1 manifest describes direct stream usage; adding global role bits
to its texture entries would not describe per-material associations correctly.
SH4ZAM numerical/ABI/performance fixtures and physical image gates remain open.

## September 12: serialized auxiliary associations

The explicit PML1 codec now associates model/source-strip ranges with the
existing lightmap/emission descriptor. It provides checked size queries,
serialization, opening, indexed decode, binary-search lookup and concrete
model-array validation. The 32-byte header and 64-byte entries are encoded
field-by-field in little endian with separate header/payload CRCs. Runtime
enum values are translated, not dumped. See [the wire contract](pvr-chunk-layers.md).

Admission rejects overlapping/overflowing ranges, invalid samplers/tints,
nonfinite UV transforms, unknown UV selectors, reserved bytes, malformed
framing and CRCs. Writes validate the entire input and capacity before any
mutation; views/accessor outputs cannot alias source bytes. Models referenced
by associations are reopened once each during load-time validation. Immutable
render-time lookups do not repeat CRC scans. No existing model/cache layout,
allocation policy or ordinary scene lifecycle changes.

The runtime helper and codec share a private metadata validator. The layered
example now serializes, opens and looks up its procedural associations before
resolving real texture-table entries and preparing recipe geometry. A new
host/target suite uses independently encoded golden bytes and fixed CRCs, plus
all truncations and single-byte corruptions, CRC-repaired malformed fields,
alias rejection, range gaps/boundaries and real model-view validation.

Automatic PCM2 scene consumption and auxiliary glTF import are deliberately
still absent. Those require a required-material admission policy, container
association, independent UV attributes and resource/recipe integration. A
generic loader ignoring this new rendering meaning must not count as a
successful import. Existing base-UV transforms must also be accounted for
before deriving a canonical-to-layer UV mapping. These are the next steps;
the codec is not a claim that the asset-import objective is complete.

Validation:

- Layer-codec and resource-binding suites pass GCC 14 GNU17/strict C23 and
  Clang GNU17/strict C2x; the codec also passes ASan/UBSan.
- Full KOS rebuild and SH-4 links of the codec test and both ordinary/layered
  material examples pass. All six public functions have module exports and
  appear in the focused Doxygen group output.
- The codec test passes Flycast interpreter and dynarec. The serialized-layer
  example passes its independent packet and submission checks in both modes.
  These are not pixel-conformance or physical-hardware validation results;
  the existing translucent presort/image gates remain open.
