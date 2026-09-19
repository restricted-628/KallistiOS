# High-level 3D capability audit

This document defines the boundary between KOS's hardware-facing PVR support
and optional reusable 3D facilities. It is a behavioral inventory, not a plan
to reproduce another development environment's symbols, file formats, global
state, or runtime architecture.

## Design boundary

The PVR driver remains the sole owner of Tile Accelerator registration,
rendering, video memory, texture transfers, render targets, and hardware
events. Higher-level code may compile materials and geometry for those APIs,
but must not create a second scene lifecycle or hide PVR ownership.

The KOS math layer owns small, generally useful SH-4 primitives. SH4ZAM is the
primary Dreamcast implementation for applicable vector, matrix, geometry,
lighting, animation, and deformation work. Established KOS entry points retain
their public names and contracts, with explicit representation bridges and
portable fallbacks where host validation requires them. Content conversion and
compression belong in host tools. PVR ownership, DMA, texture storage, TA
layout, and render-state code remain independent because they are not math
workloads.

## Capability inventory

| Family | Current state | Native KOS direction |
| --- | --- | --- |
| Matrix register operations | Fast load, store, multiply, transform, translation, rotation, scaling, perspective, and look-at helpers | Keep the register API small; eliminate shared scratch state and provide explicit caller-owned save/restore. |
| Transform hierarchy | Bounded caller-owned matrix stack plus parent-before-child compact-model traversal | Preserve explicit overflow and underflow reporting, allocation-free storage, and caller-owned hierarchy policy. |
| Camera state | Established immediate operations, checked perspective/look-at builders, and caller-owned animated camera poses | Keep camera and clock ownership in the application while sharing the established matrix contract. |
| Frustum and visibility | Caller-owned screen/W frusta provide bounded AABB classification and triangle clipping | Leave spatial partitioning, occlusion policy, and retained object state to applications. |
| Mesh submission | Checked strided projection into canonical vertices, homogeneous triangle/segment clipping, constant-width line expansion, and caller-owned memory, current-list, or explicit buffered-list sinks | Build mesh traversal over this contract without taking scene or resource ownership. |
| Materials | Checked immutable packets compile existing PVR contexts into polygon, sprite, and two-volume headers | Keep texture allocation and scene ownership in their established PVR APIs. |
| Object hierarchy | Parent-before-child compact-model traversal composes static matrices or sampled caller-owned TRS poses with bounded workspace and callbacks; explicit node policy supports component suppression, hidden nodes, and noncontiguous descendant pruning; checked PCH1 version 2 sections bind stable model ordinals into that same evaluator | Keep scene policy and model lifetime outside the traversal core. |
| Lighting | Checked inverse-transpose normals, signed directional/point diffuse lights, offset-color specular, distance-cue alpha, deterministic ARGB packing, compact render-policy presets, allocation-free binary or multiband cel partitioning, and a prepared-cache topology-aware band emitter are available | Keep light ownership, custom shading, profiles, and scene state outside the PVR driver. |
| Keyframe animation | Validated immutable tracks, clips, blended TRS poses, and explicit one-shot/loop/ping-pong cursors bind to object hierarchies, cameras, lights, visibility, events, and morph targets; checked PCM2 transform and instance-specific morph-weight sections materialize into the same caller-owned runtime | Keep system clocks, event meaning, and scene ownership in the application. |
| Skinning and morphing | Bounded additive morphing and indexed linear-blend skinning operate over caller-owned streams and palettes; explicit compact-model skin and sparse shape bindings build reusable dense sources, bind scalar weight tracks, and expose indexed completed poses; checked PCM2 general-skin, inverse-bind skeleton, and sparse morph sections preserve arbitrary influences and finite target deltas while materializing into the same runtime types | Keep pose evaluation, playback clocks, output storage, and blend policy outside the deformation kernels. |
| Sprite cells | Checked caller-owned 2D/3D cell compilation, UV regions, pivot, generic whole-motion binding, signed priority, flips, visibility compaction, multiple independently timed step streams and bounded events, material routing metadata, per-corner color, compact hardware sprites, colored strips, projection, and existing sprite sinks/materials | Core runtime complete; texture, material, scene, list, authoring, event meaning, and clock ownership remain explicit. |
| Particles | Deterministic caller-owned pools, spawn/step, sprite-cell extraction, colored/textured billboards, and camera-facing trails | Complete; no allocator, worker, clock, random source, material, scene, or list ownership. |
| Collision | Checked rays, triangles, planes, segments, spheres, capsules, AABBs, OBBs, closest-point queries, overlap tests, and bounds are available | Keep broad-phase policy, retained worlds, object ownership, and response outside the math layer. |
| Asset formats and resource binding | Bounded compact streams plus admitted caller-owned texture tables or pre-acquired fixed-slot residency sets resolve model identifiers into checked PVR surfaces and materials; versioned split-stream containers support checked raw or optional LZ4 Frame storage, direct-disc input, ordinal-addressable multi-model stream pairs, exact per-model bounds and typed optional-section ordinals, and pointer-free resource manifests, hierarchy, variable-influence skin, inverse-bind skeleton, sparse morph, transform animation, instance morph-weight animation, collision-volume, and cooked-cache sections; one coherent allocation-free scene loader cross-validates the model table and hierarchy, materializes every model into a combined caller-owned decode span, and binds contiguous model views to nodes; prepared plans provide sparse constant-time vertex lookup; ordinary, two-volume, and modifier draw caches retain decoded state or expanded topology, PVR-native packets, canonical deformation inputs, source indices, reference-pose strip bounds, and bounded modifier user words without retaining either compact stream; current-pose AABB and conservative-sphere calculation makes whole-model culling safe after deformation; host tools convert explicit triangulated OBJ or bounded glTF 2.0/GLB scenes, multi-mesh hierarchy and instancing, materials, authored per-model general-N skin and morph data, STEP/LINEAR/CUBICSPLINE TRS and morph-weight animation, prepared caches, and compiler-ready or container output | Core conversion, deterministic material-color conversion, order-preserving strip joining, multi-material texture partitioning, runtime binding, build-time admission, source/object embedding, optional storage compression, multi-model emission with per-model deformation metadata, coherent target-side scene binding, all three cached PVR layout paths, portable cache serialization/materialization, current-pose model bounds, and optional pre-deformation strip filtering are complete without a global namespace or second renderer; texture-image conversion, opacity/list policy, global topology reordering, and content authoring remain separate policy. |

The current host closure also compiles base-color images, opacity/list routing,
triangle-strip and triangle-fan topology, material-selected UV sets, and finite
base-color texture transforms. These are build-time translations into the
existing target vocabulary. VRAM placement, global topology reordering,
advanced material roles, and content authoring remain explicit policy rather
than hidden runtime ownership.

The `chunk_resources` example is the integration gate for this row: its normal
build converts source geometry and material data, joins compatible faces,
generates the immutable model translation unit, opens the validated runtime
view, checks caller-owned strip workspace against validated metadata, pins every
referenced fixed-slot texture, resolves persistent material state, and emits
through the established geometry sink before releasing residency pins.

## Resource and execution rules

- Ordinary PVR users must pay no new initialization, thread, stack, heap, or
  per-frame cost.
- Core math primitives allocate nothing and retain no mutable global scratch.
- Optional higher-level contexts use caller-owned memory or explicit creation
  calls with deterministic destruction.
- No rendering callback runs from IRQ context unless it is already part of the
  documented PVR event surface.
- Object traversal and animation must be bounded by caller-provided counts;
  malformed data reports an error rather than walking until a sentinel.
- KOS thread switches preserve the SH-4 floating-point register banks.
  Cooperative fibers on one carrier share the active matrix register and must
  restore application-owned transform state when their control flow requires
  it; no automatic fiber tax is added for non-graphics users.
- MMU state does not alter matrix ownership. Caller storage must simply remain
  mapped and accessible for the duration of the operation.

## Dependency order

1. Make existing 3D helpers free of shared mutable scratch and add a bounded,
   caller-owned matrix stack.
2. ~~Add pure, explicit transform and camera descriptions with checked
   projection construction and host-side golden vectors.~~
3. ~~Define an optional geometry input/output contract that can target direct
   store-queue or buffered PVR submission without owning the scene.~~
4. ~~Add immutable material compilation over existing PVR contexts and texture
   surfaces.~~
5. ~~Add bounded object hierarchy traversal using caller-owned transform
   workspace.~~
6. ~~Add normal transformation, directional/point lighting, and color
   packing.~~
7. ~~Add format-neutral keyframe sampling and blended object transforms.~~
8. ~~Add bounded skinning and morph-target kernels using SH4ZAM on Dreamcast
   and portable scalar validation paths on the host.~~
9. ~~Add renderer-independent collision primitives with no retained world or
   rendering dependency.~~
10. ~~Evaluate sprites, particles, and asset helpers as independent optional
    facilities rather than prerequisites for 3D rendering.~~

## First tranche

The first tranche closes two concrete gaps:

- the established 3D helpers no longer mutate process-global temporary
  matrices, so concurrent KOS threads cannot mix one another's translation,
  scale, rotation, perspective, or camera data;
- `mat_stack_t` saves and restores the active SH-4 matrix in caller-owned,
  explicitly bounded storage, reports `ENOSPC`/`ERANGE`, and provides a
  non-consuming restore operation for callback or cooperative-control-flow
  boundaries.

The stack has no initializer hook, heap allocation, worker, or idle cost.

## Second tranche

The second tranche establishes the transform and camera contracts needed by a
future geometry layer:

- `mat_compose()` computes `lhs * rhs` in ordinary caller-owned storage using
  the same column-major, post-multiply order as `mat_apply()`, permits either
  input to alias the output, and leaves XMTRX untouched;
- `mat_perspective_build()` and `mat_lookat_build()` validate finite,
  nondegenerate descriptions and change no output on failure;
- matching apply helpers build and validate first, so rejected input cannot
  partially change XMTRX;
- the finite perspective form deliberately matches the established KOS
  screen/frustum convention. A later PVR geometry layer may offer a separately
  named infinite-far projection when its clipping and depth contract is fixed.

These functions retain no camera object, allocate no memory, create no thread,
and add no work to applications that continue using the established helpers.

## Third tranche

The third tranche establishes a renderer-neutral boundary over the existing
PVR vertex and list APIs:

- `pvr_geometry_project()` consumes a bounded, strided view whose canonical
  vertex may be embedded at the start of an application structure, applies an
  explicit matrix, emits PVR screen X/Y and reciprocal-W depth, and preserves
  the remaining vertex attributes;
- `pvr_geometry_project_vertices()` extends that checked operation to declared
  complete PVR layouts, including one-block untextured and two-block textured
  two-volume vertices; the command and XYZ prefix is transformed while every
  byte of both volume attribute sets is retained;
- malformed commands, non-finite coordinates, non-positive W, range overflow,
  insufficient output, and unsafe overlap are reported without writing beyond
  the documented valid prefix;
- memory sinks write to caller-owned capacity, current-list sinks inherit the
  active PVR direct or buffered path, and explicit-list sinks append through
  the established vertex-DMA path;
- format-bound sinks count complete vertices but derive exact publication bytes
  from the declared layout, keeping 32-byte and 64-byte capacity accounting
  separate from the established canonical sink ABI;
- sinks never begin, finish, flush, sort, retain, or otherwise own a scene.

Projection and sinks retain no global state, allocate nothing, and add no cost
to applications that use only the low-level PVR interface. The projector
deliberately remains a one-input/one-output operation; callers that need
topology-changing clipping use the separately bounded frustum operation.

## Fourth tranche

The fourth tranche closes material publication and bounded visibility without
introducing a retained renderer:

- checked compilers validate complete polygon, sprite, and two-volume contexts
  before publishing an immutable, submission-ready material packet;
- failed compilation leaves the old packet unchanged, and material submission
  sends only the embedded TA block through existing current-list or explicit
  buffered-list paths;
- caller-owned frusta combine an explicit object-to-screen matrix with screen
  and W bounds matching the established KOS perspective convention;
- AABB classification transforms exactly eight corners, and triangle clipping
  uses bounded homogeneous Sutherland-Hodgman stages followed by independent
  triangle-fan output;
- clipping stages its maximum 21 vertices before writing caller memory, so
  malformed input and insufficient capacity cannot expose partial geometry.

The tranche allocates no memory, creates no worker, owns no texture or scene,
and retains no global transform or material state.

## Fifth tranche

The fifth tranche supplies the lighting primitives needed by compact-model and
application-owned mesh renderers without adding a retained light manager:

- `pvr_normal_matrix_build()` derives the inverse-transpose upper 3x3 from a
  finite, nonsingular object transform, including nonuniform scale;
- `pvr_normal_transform()` processes bounded, strided normals, normalizes each
  result, supports exact in-place operation, and reports a valid output prefix
  on malformed input;
- `pvr_environment_map_uv()` supplies a checked top-left-origin sphere-map
  projection, and the compact binding adapter turns the environment strip flag
  into an executable view-space UV policy while preserving caller callbacks;
- `pvr_lighting_apply()` validates all ambient, directional, point, range, and
  attenuation state before publication, then emits bounded diffuse Lambert
  colors from world-space samples;
- `pvr_lighting_apply_extended()` retains that allocation-free boundary while
  adding signed diffuse accumulation before saturation, positive-light
  Blinn-Phong offset color, per-vertex diffuse/specular material modulation,
  and optional caller-ranged distance cue in diffuse alpha;
- `pvr_chunk_render_policy_binding_t` binds those kernels to compact immediate
  or prepared emission through unlit, diffuse, and diffuse-plus-specular
  presets, while consuming model intensity, ambient, exponent, environment,
  and ignore-state semantics before a final application callback;
- `pvr_color_pack_argb()` clamps and rounds finite linear RGBA values into the
  established `0xAARRGGBB` representation.

Dreamcast vector transforms, normalization, reciprocal square roots, and dot
products use SH4ZAM without loading XMTRX. Portable scalar code supplies the
same checked contract to host tests. Both paths allocate nothing, create no
thread, retain no state, and perform no work unless called. The smaller Lambert
function remains unchanged for callers which do not need the extended policy.
An admitted compact binding validates its borrowed immutable lighting context
once, retaining per-sample checks without repeating constant light validation.
`pvr_toon_split_triangle()` supplies the topology-changing counterpart. It
partitions one canonical attributed triangle at any ordered set of scalar
thresholds, interpolates decoded float attributes, triangulates each band, and
reports exact bounded capacity without allocation. Binary shading is the
one-threshold case rather than a separate implementation. Compact integration
therefore remains policy above the unchanged model format.
`pvr_chunk_model_cache_emit_toon()` completes that separation at model scale:
it consumes resolved current-pose normals, uses the batched Dreamcast math
path, applies optional per-material profiles, and composes geometric band
subdivision with the existing frustum policies without allocating or adding a
renderer record to compact assets.
`pvr_chunk_model_two_volume_cache_emit_toon()` extends that same policy across
the format-bound prepared cache. Both UV/diffuse/offset parameter sets are
interpolated independently through shade subdivision and homogeneous frustum
clipping, then recombined into the original hardware packet. Independent
outside/inside ramps preserve modifier-selected surface intent rather than
reducing a two-volume model to an ordinary vertex layout.
`pvr_chunk_model_cache_emit_outline()` adds the complementary inverted-shell
silhouette pass. It expands smooth strips along current vertex normals, derives
face normals for flat strips, and leaves opposite-face culling in the explicit
material callback. The low-level normal extrusion uses SH4ZAM on Dreamcast.
The matching prepared-cache wireframe policy enumerates unique strip edges,
offers full-mesh, boundary, and path topology, clips centerlines before
constant-pixel-width expansion, and preserves the same deformation and sink
ownership boundary.

## Sixth tranche

The sixth tranche adds format-neutral animation math without introducing an
engine-owned runtime:

- `anim_track_open()` admits bounded strided scalar, vector, or quaternion
  keys only after checking finite values, nonzero rotations, strictly
  increasing time, complete address ranges, and supported interpolation;
- admitted immutable views clamp outside times to their endpoints and use a
  binary interval search, avoiding a full key scan on every frame;
- scalar and vector tracks provide step, linear, or time-aware Catmull-Rom
  interpolation. Quaternion tracks normalize inputs and use shortest-path
  spherical interpolation; explicit XYZ/ZXY Euler vector tracks unwrap each
  active interval over the shortest angular arcs before converting to the
  existing quaternion result;
- `anim_transform_sample()` combines optional translation, rotation, and scale
  channels with caller-owned fallback state; `anim_transform_blend()` blends
  two complete object transforms;
- `anim_transform_matrix_build()` publishes an explicit column-major
  `translation * rotation * scale` matrix without changing XMTRX.
- admitted clips group corresponding transform tracks under an explicit play
  interval, and caller-owned playback cursors provide one-shot, loop, and
  ping-pong policy with constant-time large-delta advance;
- clips sample complete local-matrix arrays that feed compact-model hierarchy
  traversal directly, while cross-clip sampling supplies allocation-free
  motion linking;
- camera and light tracks publish established KOS camera matrices and
  `pvr_light_t` state rather than creating a parallel scene representation.
- step visibility channels publish bounded per-transform state, event tracks
  collect crossed markers with arithmetic large-loop counting, and scalar
  morph channels publish existing `pvr_morph_target_t` bindings.

Dreamcast vector, quaternion, trigonometric, reciprocal-square-root, and matrix
construction paths use SH4ZAM. The scalar host path serves as a test oracle.
System-clock choice, event meaning and dispatch, storage, clip lifetime, and
scene ownership remain application policy. The playback cursor is opt-in caller
storage and creates no thread or fiber.

## Seventh tranche

The seventh tranche closes reusable vertex deformation without introducing
skeleton, pose, or model ownership:

- `pvr_morph_apply()` blends finite additive position and normal deltas over
  bounded, strided streams, normalizes the result, and reports the valid output
  prefix if per-vertex arithmetic fails;
- `pvr_skin_apply()` preflights every active joint index, weight, and palette
  matrix before publishing output, then performs normalized four-influence
  linear-blend skinning for positions and inverse-transpose normals;
- both kernels support exact canonical in-place deformation, reject unsafe
  overlap, allocate nothing, retain no state, and preserve point/vector W
  conventions;
- Dreamcast joint transforms and reciprocal-square-root normalization use
  SH4ZAM without changing XMTRX, while the portable scalar path provides the
  host-test oracle.

The caller continues to own skeleton topology, palette construction, pose
evaluation, animation clocks, model binding, and output storage.

## Eighth tranche

The eighth tranche connects admitted compact streams to ordinary PVR polygon
lists without introducing scene, texture, or model ownership:

- `pvr_chunk_model_emit()` preflights the complete polygon stream, support
  boundary, state ranges, indexed vertices, largest-strip workspace, and total
  memory-sink capacity before the first callback or output;
- persistent blend, mipmap, exponent, texture, and material records become a
  format-neutral draw state, while a synchronous strip callback resolves asset
  texture identifiers into application-owned KOS texture surfaces and material
  headers;
- indexed references become canonical vertices with deterministic UV and color
  precedence, reversed strips preserve winding, and an optional callback can
  apply application lighting, intensity, metadata, or user-word policy;
- every strip is projected through the established SH4ZAM-backed geometry path
  and emitted to a caller-selected memory, current-list, or buffered-list sink;
- `pvr_chunk_model_emit_two_volume()` keeps outside/inside texture and material
  state distinct, expands both UV sets, and emits the matching complete
  untextured or textured two-volume PVR layout through a format-bound sink;
- `pvr_chunk_model_emit_modifiers()` expands compact triangle, quad, and strip
  topology into winding-correct modifier packets, applies explicit list,
  culling, and include/exclude policy, and submits each header/triangle pair as
  one operation; and
- bump-material records decode their persistent signed-normalized direction
  and up basis into the ordinary render state, with an explicit vertex-policy
  callback required to combine it with application light and strength; and
- cached-polygon controls fail with `ENOTSUP` before side effects instead of
  being skipped or rendered under an incompatible contract.
- optional caller-owned model plans divide the 16-bit vertex namespace into
  sparse 256-entry pages, replacing every repeated vertex-record scan in
  ordinary, two-volume, and modifier emission with constant-time lookup while
  leaving the zero-preparation immediate path intact.

The renderer allocates nothing, creates no worker, retains no texture lookup or
model index, and never begins or finishes a PVR scene. The caller supplies one
canonical vertex workspace sized to the largest strip.

## Ninth tranche

The ninth tranche adds a renderer-independent collision geometry foundation:

- unit planes are built from bounded finite point triples and provide signed
  point projection without changing caller output on failure;
- point/segment and segment/segment closest-point queries handle parallel and
  zero-length segments, publish clamped parameters, and normalize point W;
- inclusive sphere, capsule, and axis-aligned-box overlap tests share those
  kernels rather than introducing a second collision world or object model;
- sphere, capsule, and bounded strided point streams produce checked
  caller-owned AABBs; and
- Dreamcast dot products and magnitudes use SH-4 vector instructions while the
  same source retains a strict portable host-validation path.

The layer allocates nothing, creates no worker, retains no registration or
global state, and has no relationship to PVR scene ownership. Applications
remain responsible for broad-phase acceleration, collision filtering, object
lifetime, response, and spatial partitioning. Sprites, particles, asset
conversion, broad-phase acceleration, and collision response remain independent
follow-up evaluations rather than hidden prerequisites of this ABI.

## Tenth tranche

The tenth tranche extends the same collision layer without introducing a
second world, model, or renderer owner:

- non-unit caller rays are normalized internally and publish world-space
  distances, bounded hit points, winding-preserving normals, and barycentric
  triangle weights;
- point-to-triangle queries cover face, edge, and vertex Voronoi regions and
  leave caller output unchanged when a triangle is malformed;
- ray/AABB and ray/OBB slab queries publish entry and exit intervals, including
  an entry distance of zero for origins already inside a volume;
- oriented boxes require an explicit orthonormal basis and nonnegative half
  extents, overlap spheres and AABBs, and use all fifteen separating axes for
  OBB/OBB queries; and
- OBB bounds and every boolean query scale intermediate coordinates before
  arithmetic whose naive form could overflow on finite input.

The implementation remains synchronous, allocation-free,
renderer-independent, and optimized through the existing Dreamcast vector-dot
path. The caller still owns collision filtering, response, object lifetime,
spatial partitioning, and all persistent world state.

## Eleventh tranche

The eleventh tranche closes runtime model/texture binding without introducing
a global resource manager:

- bounded sorted tables map compact 13-bit identifiers to application-owned
  checked texture surfaces with allocation-free binary lookup;
- table admission validates strict identifier ordering, complete array and
  VRAM ranges, surface metadata, capacity, and 4-bit/8-bit palette selectors;
- `pvr_chunk_material_resolve()` combines one application-owned base context
  with decoded compact blend, texture, mipmap, UV, supersampling, alpha,
  shading, culling, and specular state, then publishes an established checked
  one- or two-volume material;
- `pvr_chunk_material_binding_begin_strip()` is a direct renderer callback
  that submits the resolved header through the established current-list or
  explicit buffered-list path; and
- missing resources, mutated surfaces, incompatible contexts, and malformed
  state fail before changing the destination material or submitting a header.

The application retains texture allocation, upload, palette contents, model
storage, resource names, scene and list ownership, and lifetime policy. The
adapter allocates nothing, retains no global state, and creates no
initialization or idle cost for applications that do not use compact-model
resources.

The optional fixed-slot adapter extends this boundary without moving ownership:

- one caller-owned pin set scans one or more admitted models before list
  emission and acquires every distinct resident identifier;
- missing, loading, or over-capacity resources fail before the corresponding
  model writes geometry;
- the material callback resolves only the sorted pre-acquired set and never
  performs cache admission from inside an active list; and
- the caller releases the generation-checked pins after the render ticket or
  equivalent completion boundary proves sampling has ended.

This connects compact-model rendering to bounded texture replacement while
leaving decompression, upload, frame prediction, and scene lifetime outside the
renderer.

## Twelfth tranche

The twelfth tranche closes compact-model shape motion without introducing a
second animation system or renderer:

- sparse target records identify canonical finite position and normal deltas
  by original 16-bit model vertex index;
- binding re-admits the model, validates the caller-owned sparse plan, and
  rejects duplicate, unordered, or absent target indices before publishing a
  constant-time dense lookup;
- one exact caller workspace retains base vertices and target-major dense
  deltas, so neither compact vertices nor sparse targets are decoded per frame;
- each target binds to an optional established scalar animation track plus a
  finite fallback weight, and the sampled result feeds the existing bounded
  morph kernel; and
- completed poses resolve original model indices directly for immediate or
  cached model emission, with sampled target order and identity checked before
  output is modified.

The layer allocates nothing, owns no clip, cursor, clock, scene, or model, and
creates no worker, fiber, or idle cost. Applications that do not bind shape
data retain the established compact-model path unchanged.

## Validation gates

The `chunk_scene` integration fixture exercises the coherent scene loader with
two models, a serialized two-joint skeleton, transform playback, opposing
instance-specific morph curves, and ordinary cooked caches. Its admission,
pose, and memory-sink checks run from the same source on host and target.
Independent numerical expectations cover six times, world-space skin output,
and preserved per-model colors. This complements `chunk_asset`'s compressed
service-loading, residency, and lighting composition; neither example adds
retained scene ownership or constitutes exhaustive import conformance.

Each tranche requires host tests for structure, bounds, interpolation, and
packing; a complete Dreamcast cross-build; focused emulator execution for the
public example; and an explicit physical-hardware list for timing or numerical
behavior an emulator cannot establish. Optimized and baseline math backends
must agree within a documented floating-point tolerance rather than by raw
bit identity where operation ordering differs.
