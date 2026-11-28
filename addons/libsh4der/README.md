# SH4DER-PVR for KOS Experimental

Pinned dependency: [JiltedValkyrie/SH4DER-PVR](https://github.com/JiltedValkyrie/SH4DER-PVR)
at `63e686b339ded85732d850e8efcc1a898d4933a5`. The upstream checkout is
unmodified. Initialize it with `git submodule update --init --recursive`.
When upgrading a built checkout from `32912914`, clean this addon before
rebuilding: old dependency files name sources under `sh4dow-dc/src/` that
now live in `src/`. Recompile consumers of the inline headers as well.

This addon supplies effect computations to the existing KOS Experimental
renderer. It does not install a renderer, acquire PVR ownership, begin scenes,
allocate VRAM, sample the scanout buffer, change fog or manage render targets.
Applications explicitly link `-lsh4der`; KOS already supplies SH4ZAM.
There is no new global fast-math policy.

For independently lit Compact material volumes, include
`<sh4der_kos_routed_lighting.h>` and link `-lsh4der`. Prepare a caller-owned
plan when the light set or material routing changes. Each mask bit selects
one of up to three directional lights. For textured packets, each volume
has a base and an additive offset mask. Color-only packets accept base masks.
Pass `sh4der_kos_routed_lighting_vertex` and the plan to the admitted
two-volume draw. Directions and normals must be unit length in the same
coordinate space. The callback replaces the selected RGB fields and preserves
their alpha bytes. A zero mask gives black unless the caller supplies ambient.
The optional ambient pair can be inherited or explicitly overridden inside.

The reusable `<sh4der_kos.h>` adapter binds model-indexed sidecars once and
composes blade/holo, AO and posed limb shadows through Compact's existing
callback. See [coverage and contracts](COVERAGE.md) for exact available features
and validation limits. For a prepared UV warp, use
`<sh4der_kos_uvwarp.h>` directly as the Compact vertex callback or chain it
before an existing color effect. Install it only after prepare succeeds.
For reflections, water, transmission, heightfield shadows, light pools and
sparks use `<sh4der_kos_world.h>`. The [Compact world example](../../examples/dreamcast/pvr/sh4der_world/)
shows the two KOS passes and their resource lifetimes. With the fork environment
sourced, build `addons/libsh4der` and then the example with make.
The upstream planar, spark, bolt, mote and PVRX submission or device backends
are not linked. Use the fork's PVR interfaces for scene and device ownership.
The vendored SH4ZAM-DCE is not installed as a second math provider.

The accepted build profile is the fork's ordinary `-O2` without fast-math.
Inline helpers also inherit their application's flags. The bundled foot-shadow
host test fails its exact fade-height endpoint under upstream-style fast-math;
that profile is not validated here. Do not interpret a successful compile as
numerical parity at those boundaries.

## Included computations

The archive builds upstream `sh4der_core`, `sh4der_blade`, `sh4der_holo`,
`sh4der_bloom`, the CPU mesh portion of `sh4der_torch`, and the bundled `sh4dow_dc` and `sh4dow_foot` CPU sources.
Use headers such as `<sh4der/sh4der_blade.h>` and `<sh4dow_dc.h>`.

- Blade/beam energy and hologram vertex colors: prepare once per instance/frame,
  then feed colors through Compact's existing `prepare_vertex` callback.
- Geometric bloom: caller-owned positions, intensities and triangle indices;
  route through KOS geometry projection, clipping and submission.
- Limb self-shadow and foot-cone geometry: caller supplies posed limbs, collision
  plane, buffers and draw policy. SH4DOW's XMTRX batch helpers replace matrix
  state; finish that batch and restore the renderer's matrix before drawing.
- Header-only calculations (grading/exposure, AO preparation and effect mesh
  helpers) are available without enabling their upstream GPU backends.

Static bind-pose hologram weights and applicable static AO should be prepared
on the host or at asset load, not rebuilt for every frame. Timed effects use
their documented tick/time units, not an assumed display frame rate.

## Native effect integration

The upstream native framebuffer, glow, warp, trail, lens, focus, visor, torch GPU,
chromatic-aberration and shafts source files are **not** in this archive.
Headers can declare their GPU functions, but those functions are not provided
by this adapter. Do not separately link the upstream all-in-one archive to
fill them in: it would reintroduce competing state and resource ownership.

Use the `sh4der_kos_*` headers for retained-frame sampling, glow, warp, trail/TAA,
lens, focus, visor, torch, chromatic aberration, shafts, exposure and screen
cavity. They prepare caller storage and complete packets/meshes for existing KOS
sinks. Images/history are explicitly borrowed; producer completion does not
retire later readers. Bind/rebind at resource boundaries and reset history on
cuts, resize and PVR restart. Fog and global texture stride remain caller-owned.

Host-bake static weights, crease AO, masks, LUTs and fixed geometry. Native
checks under `utils/sh4der-*-test` cover original retained RTT fixtures and
Compact poses; normal/sanitizer host checks, SH-4 links and emulator readback
are separate from physical-hardware visibility and performance qualification.
Glow feedback is an estimate after bilinear filtering/quantization/clipping;
use pre-effect capture for exact source data. See COVERAGE.md for contracts.

## Licensing and attribution

Read [SH4DER-PVR's license](upstream/LICENSE) and the bundled
[SH4DOW-DC license](upstream/sh4dow-dc/LICENSE). These are custom licenses,
**not MIT**. Both require attribution/notice preservation and restrict sale
of the original or substantially similar derivatives as standalone libraries,
plugins or software packages. Their stated permission for incorporation in
larger personal/commercial products does not remove those conditions.

Preserve credits for Jilted Valkyrie (SH4DER-PVR and SH4DOW-DC),
and Falco Girgis and contributors (SH4ZAM, separately MIT licensed). This
adapter does not relicense any dependency under KOS's license.
