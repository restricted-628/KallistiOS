# SH4DER-PVR for KOS Experimental

Pinned dependency: [JiltedValkyrie/SH4DER-PVR](https://github.com/JiltedValkyrie/SH4DER-PVR)
at `17a1f0623505f715824ab8cee85d45f16b9c3ebc`. The upstream checkout is
unmodified. Initialize it with `git submodule update --init --recursive`.

This addon supplies effect computations to the existing KOS Experimental
renderer. It does not install a renderer, acquire PVR ownership, begin scenes,
allocate VRAM, sample the scanout buffer, change fog or manage render targets.
Applications explicitly link `-lsh4der`; KOS already supplies SH4ZAM.
There is no new global fast-math policy.

The accepted build profile is the fork's ordinary `-O2` without fast-math.
Inline helpers also inherit their application's flags. The bundled foot-shadow
host test fails its exact fade-height endpoint under upstream-style fast-math;
that profile is not validated here. Do not interpret a successful compile as
numerical parity at those boundaries.

## Included computations

The archive builds upstream `sh4der_core`, `sh4der_blade`, `sh4der_holo`,
`sh4der_bloom`, and the bundled `sh4dow_dc` and `sh4dow_foot` CPU sources.
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

## Deliberately not linked yet

The upstream native framebuffer, glow, warp, trail, lens, focus, visor, torch,
chromatic-aberration and shafts source files are **not** in this archive.
Headers can declare their GPU functions, but those functions are not provided
by this adapter. Do not separately link the upstream all-in-one archive to
fill them in: it would reintroduce competing state and resource ownership.

Full-screen effects need caller-owned render-target/scanout descriptors,
format and stride validation, a single submission path, and explicit render
completion/retirement before reuse or free. History effects additionally need
frame identity and resize/reset handling. Fog remains renderer-owned. These
are separate integration work, not capabilities claimed complete here.

## Licensing and attribution

Read [SH4DER-PVR's license](upstream/LICENSE) and the bundled
[SH4DOW-DC license](upstream/sh4dow-dc/LICENSE). These are custom licenses,
**not MIT**. Both require attribution/notice preservation and restrict sale
of the original or substantially similar derivatives as standalone libraries,
plugins or software packages. Their stated permission for incorporation in
larger personal/commercial products does not remove those conditions.

Preserve credits for Jilted Valkyrie (SH4DER-PVR), Emerald May (SH4DOW-DC),
and Falco Girgis and contributors (SH4ZAM, separately MIT licensed). This
adapter does not relicense any dependency under KOS's license.
