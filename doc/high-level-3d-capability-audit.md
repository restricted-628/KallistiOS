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

The KOS math layer owns small, generally useful SH-4 primitives. An optional
3D library may own cameras, object traversal, mesh preparation, lighting, and
animation. Content conversion and compression belong in host tools. Optimized
third-party math libraries may accelerate the optional layer through explicit
adapters, but neither the kernel nor the PVR driver may depend on them.

## Capability inventory

| Family | Current state | Native KOS direction |
| --- | --- | --- |
| Matrix register operations | Fast load, store, multiply, transform, translation, rotation, scaling, perspective, and look-at helpers | Keep the register API small; eliminate shared scratch state and provide explicit caller-owned save/restore. |
| Transform hierarchy | No bounded matrix stack | Add a caller-owned stack with reported overflow and underflow and no allocation. |
| Camera state | Established immediate operations plus explicit, checked perspective and look-at builders | Keep application state caller-owned; add higher-level camera policy only outside the core math layer. |
| Frustum and visibility | No reusable object/frustum tests | Add pure math tests independently of rendering; leave spatial partitioning to applications. |
| Mesh submission | Checked strided projection into canonical vertices plus caller-owned memory, current-list, and explicit buffered-list sinks | Build future mesh traversal over this contract without taking scene or resource ownership. |
| Materials | PVR contexts and compiled headers are complete at the hardware level | Add optional immutable material descriptions that compile to existing PVR headers; do not introduce another texture allocator. |
| Object hierarchy | No reusable traversal | Add bounded, callback-driven traversal over application-owned nodes after transform state is complete. |
| Lighting | Hardware header fields exist, but vertex lighting is application work | Add optional CPU vertex-lighting kernels with explicit normal-transform and color-clamp contracts. |
| Keyframe animation | No generic sampler | Add format-neutral scalar, vector, and rotation sampling before object-specific motion playback. |
| Skinning and morphing | No generic deformers | Build bounded kernels over caller-supplied streams; allow an optimized math backend without requiring it. |
| Sprites and particles | PVR sprite primitives exist | Keep emitters and lifetime policy in an optional utility library, not the driver. |
| Collision | General-purpose geometry concern | Keep collision independent of the renderer so headless and non-PVR users can consume it. |
| Asset formats and resource names | No engine-owned model namespace | Use open, documented formats or application adapters. Do not embed legacy binary formats or global-ID managers in KOS. |

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
4. Add immutable material compilation over existing PVR contexts and texture
   surfaces.
5. Add bounded object hierarchy traversal using the caller-owned transform
   stack.
6. Add normal transformation, directional/point lighting, and color packing.
7. Add format-neutral keyframe sampling and blended object transforms.
8. Add bounded skinning and morph-target kernels with optional optimized math
   adapters.
9. Evaluate sprites, particles, collision, and asset helpers as independent
   optional libraries rather than prerequisites for 3D rendering.

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
- malformed commands, non-finite coordinates, non-positive W, range overflow,
  insufficient output, and unsafe overlap are reported without writing beyond
  the documented valid prefix;
- memory sinks write to caller-owned capacity, current-list sinks inherit the
  active PVR direct or buffered path, and explicit-list sinks append through
  the established vertex-DMA path;
- sinks never begin, finish, flush, sort, retain, or otherwise own a scene.

Projection and sinks retain no global state, allocate nothing, and add no cost
to applications that use only the low-level PVR interface. Automatic clipping
is intentionally outside this tranche; callers must provide geometry with
positive transformed W or perform clipping before projection.

## Validation gates

Each tranche requires host tests for structure, bounds, interpolation, and
packing; a complete Dreamcast cross-build; focused emulator execution for the
public example; and an explicit physical-hardware list for timing or numerical
behavior an emulator cannot establish. Optimized and baseline math backends
must agree within a documented floating-point tolerance rather than by raw
bit identity where operation ordering differs.
