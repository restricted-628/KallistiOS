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
| Transform hierarchy | No bounded matrix stack | Add a caller-owned stack with reported overflow and underflow and no allocation. |
| Camera state | Established immediate operations plus explicit, checked perspective and look-at builders | Keep application state caller-owned; add higher-level camera policy only outside the core math layer. |
| Frustum and visibility | Caller-owned screen/W frusta provide bounded AABB classification and triangle clipping | Leave spatial partitioning, occlusion policy, and retained object state to applications. |
| Mesh submission | Checked strided projection into canonical vertices plus caller-owned memory, current-list, and explicit buffered-list sinks | Build future mesh traversal over this contract without taking scene or resource ownership. |
| Materials | Checked immutable packets compile existing PVR contexts into polygon, sprite, and two-volume headers | Keep texture allocation and scene ownership in their established PVR APIs. |
| Object hierarchy | Parent-before-child compact-model traversal composes caller-owned transforms with bounded workspace and callbacks | Keep scene policy and model lifetime outside the traversal core. |
| Lighting | Checked inverse-transpose normals, directional and point Lambert lights, and deterministic ARGB packing are available as allocation-free CPU kernels | Keep light ownership, material policy, and advanced shading outside the PVR driver. |
| Keyframe animation | Validated immutable scalar, vector, and quaternion tracks provide clamped logarithmic sampling plus blended TRS object transforms | Keep playback clocks, looping policy, clip ownership, and object binding outside the math core. |
| Skinning and morphing | Bounded additive morphing and indexed linear-blend skinning operate over caller-owned streams and palettes | Keep skeleton ownership, pose evaluation, mesh binding, and blend policy outside the deformation kernels. |
| Sprites and particles | PVR sprite primitives exist | Keep emitters and lifetime policy in an optional utility library, not the driver. |
| Collision | Checked planes, segments, spheres, capsules, AABBs, closest-point queries, overlap tests, and bounds are available | Keep broad-phase policy, retained worlds, object ownership, and response outside the math layer. |
| Asset formats and resource names | Bounded compact chunk streams provide validated structure without an engine-owned namespace | Keep parsing and traversal in KOS-native, caller-owned APIs; reject malformed offsets and counts before rendering. |

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
10. Evaluate sprites, particles, and asset helpers as independent optional
    libraries rather than prerequisites for 3D rendering.

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
- `pvr_lighting_apply()` validates all ambient, directional, point, range, and
  attenuation state before publication, then emits bounded diffuse Lambert
  colors from world-space samples;
- `pvr_color_pack_argb()` clamps and rounds finite linear RGBA values into the
  established `0xAARRGGBB` representation.

Dreamcast vector transforms, normalization, reciprocal square roots, and dot
products use SH4ZAM without loading XMTRX. Portable scalar code supplies the
same checked contract to host tests. The layer allocates nothing, creates no
thread, retains no state, and performs no work unless called.

## Sixth tranche

The sixth tranche adds format-neutral animation math without introducing an
engine-owned clip or playback system:

- `anim_track_open()` admits bounded strided scalar, vector, or quaternion
  keys only after checking finite values, nonzero rotations, strictly
  increasing time, complete address ranges, and supported interpolation;
- admitted immutable views clamp outside times to their endpoints and use a
  binary interval search, avoiding a full key scan on every frame;
- scalar and vector tracks provide step or linear interpolation, while
  quaternion tracks normalize inputs and use shortest-path spherical
  interpolation;
- `anim_transform_sample()` combines optional translation, rotation, and scale
  channels with caller-owned fallback state; `anim_transform_blend()` blends
  two complete object transforms;
- `anim_transform_matrix_build()` publishes an explicit column-major
  `translation * rotation * scale` matrix without changing XMTRX.

Dreamcast vector, quaternion, trigonometric, reciprocal-square-root, and matrix
construction paths use SH4ZAM. The scalar host path serves as a test oracle.
Playback time, looping, events, hierarchy binding, and clip lifetime remain
application policy.

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
lifetime, response, and spatial partitioning. Ray and triangle queries,
oriented volumes, sprites, particles, and asset conversion remain independent
follow-up evaluations rather than hidden prerequisites of this ABI.

## Validation gates

Each tranche requires host tests for structure, bounds, interpolation, and
packing; a complete Dreamcast cross-build; focused emulator execution for the
public example; and an explicit physical-hardware list for timing or numerical
behavior an emulator cannot establish. Optimized and baseline math backends
must agree within a documented floating-point tolerance rather than by raw
bit identity where operation ordering differs.
