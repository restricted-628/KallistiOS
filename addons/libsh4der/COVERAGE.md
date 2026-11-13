# SH4DER and SH4DOW integration coverage

Pinned source: SH4DER `17a1f062`, including bundled SH4DOW. This inventory tracks
the full integration request; available math headers alone do not close a row.

| Capability | Native integration | Remaining work |
| --- | --- | --- |
| Core, blade, holo | CPU archive; reusable `sh4der_kos_vertex`; Compact example | Native visual validation |
| Bloom | CPU archive; caller geometry in example | Reusable geometry builder and native visuals |
| Static AO | Indexed retained-light sidecar layered by Compact adapter | Host authoring fixture for crease bake |
| SH4DOW limbs | Prepared columns consumed in Compact callback; bone exclusion; same-space contract | Posed skeleton consumer and native visuals |
| SH4DOW feet | CPU library only | Caller-owned placement/receiver geometry and texture adapter |
| Look/grade | Math only | Ordered caller-owned header/quad builder |
| Frame sampling | Math only | Explicit retained surface, stride/format and capture admission |
| Glow | Math only | Caller-owned stage/upload/mesh path, reader retirement |
| Warp | Math only | Caller mesh/sink adapter and scratch |
| Trail/TAA | Math only | Explicit history surfaces, identity/reset/resize, lifetime |
| Lens | Math only | Caller-owned grain/vignette state and sink packets |
| Focus | Math only | Caller blur surface and mesh |
| Visor | Math only | Caller thermal/grain/scanline resources and layers |
| Torch | Header math only | CPU mesh object split/adaptation, caller fog application |
| CA | Math only | Explicit RTT surfaces and channel passes |
| Shafts | Math only | Caller map/upload/mesh and lifetime |

No screen backend is claimed integrated yet. KOS owns scene/list routing,
allocation, fog, stride and render tickets. New adapters must use one sink for
headers and vertices, retain producers and readers before resource reuse, and
avoid hidden state. Ordinary ordered GPU passes do not imply CPU waits between
every pass. Host tests, SH-4 links, emulation and hardware remain distinct.

## Compact binding and composition

Include `<sh4der_kos.h>`, link `-lsh4der`, and bind sidecars with
`sh4der_kos_bind` against an already admitted model view. Binding uses the
view's existing maximum-index summary; it does not parse or scan the asset
again. Arrays include holes up through that maximum, not just densely numbered
vertices. A failed bind leaves the old binding intact. Rebind whenever streams,
sidecar storage or vertex ordering change, even at the same address. Generation
and replacement ownership stay with the scene's owner.

Construct one `sh4der_kos_effect_t` per draw policy. Choose zero or one blade/holo
frame, an explicit REPLACE/MODULATE/ADD policy, and optional prepared limb shadow.
Holo requires the bound weight sidecar. `base_prepare` can run existing lighting
or deformation first; a negative result stops composition. RGB modulation/addition
preserves base alpha; replacement takes effect alpha. AO and shadow then attenuate
RGB, preserving offset color, UVs and command bits. Material groups and additive
or alpha list/header selection remain the caller's existing strip policy.

Positions after `base_prepare`, posed limb centers and light direction must be
in the same coordinate space. Refresh `shadow_axes = sh4dow_axes(shadow)` when
the prepared shadow changes. This callback uses scalar vector operations without
loading XMTRX. It does not call the vendor's matrix-clobbering batch helper.
An application choosing that batch helper instead must bracket its whole batch
with matrix ownership/restoration, never save/restore per vertex.

Host-cook static holo weights/AO and bind once. Dynamic effect frames and posed
shadows update when their inputs change. Bindings and frames are immutable while
callbacks use them. These are admitted callback preconditions, not repeated
per-vertex asset validators. Output capacity stays with existing KOS sinks.
