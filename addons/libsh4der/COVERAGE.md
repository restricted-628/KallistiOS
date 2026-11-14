# SH4DER and SH4DOW integration coverage

Pinned source: SH4DER `17a1f062`, including bundled SH4DOW. This inventory tracks
the full integration request; available math headers alone do not close a row.

| Capability | Native integration | Remaining work |
| --- | --- | --- |
| Core, blade, holo | CPU archive; reusable `sh4der_kos_vertex`; native sparse Compact/posed consumer | Hardware visibility/performance |
| Bloom | Caller geometry builder, projection/sink and native additive consumer | Hardware visibility/performance |
| Static AO | Reproducible host crease bake and indexed retained-light sidecar in native Compact consumer | Hardware visibility/performance |
| Screen cavity | Caller scratch/map from retained grid, multiply image and native consumer | Hardware visibility/performance |
| SH4DOW limbs | Prepared columns in native Compact skin poses, real first-bone exclusion and shared space | Hardware visibility/performance |
| SH4DOW feet | Host-cooked alpha mask, posed receiver geometry, depth-tested textured native consumer | Hardware visibility/performance |
| Look/grade | Cached dynamic grade, prepared histogram/exposure with exact frame metadata; native consumer | Hardware visibility/performance |
| Frame sampling | Admitted RGB565 linear/stride surface, sparse grid read, exact metadata history and integrated native effect consumers | Hardware visibility/performance |
| Glow | Caller-owned maps/history/upload and halo/ray/shaft packets; exact-map halo feedback correction and native consumer | Hardware visibility/performance |
| Warp | Prepared grid, caller scratch/mesh, radial blur, pixelation and dither packets; native stride/twiddled consumer | Hardware visibility/performance |
| Trail/TAA | Explicit retained history slots, exact producer IDs, cuts/reset/resize, cached keep/copy packets and native ping-pong consumer | Hardware visibility/performance |
| Lens | Host/load vignette and grain bake, caller headers/state, grain/hit/flash packets; native consumer | Hardware visibility/performance |
| Focus | Admitted mask, caller RGB565 box blur, alpha mesh and image upload; native three-shape consumer | Hardware visibility/performance |
| Visor | Host/load preset/LUT/pattern/mask plan, caller thermal/grade/grain/scan/vignette output; native modes | Hardware visibility/performance |
| Torch | Prepared cone mesh and fog reach curve, caller-owned fog-table application; native consumer | Hardware visibility/performance |
| CA | Prepared viewport and three complete channel packets over admitted image; native stride consumer | Hardware visibility/performance |
| Shafts | Prepared light-distance field, caller shade map/history, multiply mesh and native retained-source consumer | Hardware visibility/performance |

All pinned effect families have KOS adapters and native consumer coverage.
Hardware visibility and performance qualification remain separate from implementation. KOS owns scene/list routing,
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

## Geometry builders

`sh4der_kos_geometry.h` expands prepared bloom into independent triangle strips
and projects a posed foot onto a current receiver plane using SH4DOW's cone
math. Output remains in object/world coordinates for existing KOS projection
and clipping. Capacity failure or a rejected foot leaves the destination intact.
Supply aligned arrays when passing the results to KOS geometry sinks.

`sh4der_kos_foot_alpha4444` bakes a linear black alpha mask on the host or at
load. The caller admits texture dimensions/layout and owns upload/retirement.
Use MODULATEALPHA and SRCALPHA/INVSRCALPHA with depth writes disabled, retaining
depth testing against the scene. Receiver data must be refreshed when its
surface changes. The constructor does not authorize a stale collision hit,
override list policy or solve transparent receiver ordering.

## Grade packets

`sh4der_kos_screen.h` builds up to six aligned header/quad packets in solver
order. Dynamic values and output capacity are admitted before writing anything.
All packets disable fog/depth writes and use depth ALWAYS. Submit complete
packets through one current or explicit buffered TR route. With autosort, reserve
increasing reciprocal depths in front of scene content; with manual sorting,
submit in array order. The builder rejects indistinguishable float depths.
Constant authored grades can be built once, as in the Compact example. Adaptive
exposure uses the retained frame and exact metadata through sh4der_kos_look.h.

## Retained frame sampling

`sh4der_kos_frame.h` admits a caller-retained RGB565 linear/stride surface and
precomputes sample offsets once. It reads 4800 texels (9600 with dark neighbors),
without staging a full frame, querying scanout registers or repeating descriptor
validation per pixel. CPU reads require the current producer COMPLETE and return
EAGAIN otherwise; ordinary ordered GPU passes do not need this CPU wait.

The binding borrows storage; it does not pin an allocation or track its readers.
The owner serializes it against writes/release/shutdown and supplies the latest
writer ticket. Change the nonzero generation and rebind on replacement, resize
or PVR restart, even at the same address. A completed producer cannot retire
later GPU sampling readers. The optional eight-slot metadata history uses exact
generation/render IDs and rejects evicted IDs; it does not retain GPU textures.
No three-bit stamp is used as a resource identity.

`utils/sh4der-frame-test` has host failure/arithmetic coverage and an original
RGB565 RTT fixture. Real scene execution, cache visibility and cost remain
separate validation levels; frame sampling alone does not complete screen effects.

## Glow maps and halo

`sh4der_kos_glow.h` builds a padded 128x64 bright map and 32x16 shrink from an
80x60 RGB565 grid. Supply the 256-byte LUT cooked on the host or prepared once
at load. Caller-owned, aligned staging holds both maps and the sampled frame ID.
Optional easing uses an earlier sample in the same generation; duplicate/stale
easing inputs fail. Preparation supports in-place history, clears padding, and
does not mutate the input grid. Use a pre-glow source, or
correct composited samples with feedback_record/remove using their exact retained
render metadata and immutable map contents before preparation.

Bind two distinct caller-owned RGB565 linear surfaces once to compile invariant
headers. Upload uses existing blocking CPU/SQ/DMA operations. The last render-use
ticket must cover the destinations' producers AND readers; source-frame
completion alone says nothing about those destinations. Omitted last-use ticket
is the caller's assurance of no outstanding use, not an inferred idle state.
The helper does not track submitted consumers. Keep descriptors/storage stable,
serialize submission/update/shutdown and rebind on replacement or PVR restart.

An incomplete last use returns EAGAIN before mutation. Once upload starts, any
error invalidates readiness; a complete pair must succeed before new halo packets
can be built or old packets submitted. The first texture may already have changed.
The caller retains responsibility for already compiled packets and GPU readers.
No allocation, rollback, scene owner, hidden scheduler or per-pixel layout check
is introduced.

Fixed texel-center UVs are compile-time data. Reuse completed packets when
rectangle, weights and depth policy are unchanged. Halo output is up to three
complete grade-compatible packets: darken, large
map, small map, with bilinear texel-center UVs, fog off, depth ALWAYS/no writes.
Strength above one uses explicit passes. The caller reserves depths/order and
submits each whole packet through the same KOS sink. Host tests cover arithmetic,
header contexts and failure semantics; `utils/sh4der-glow-test` exercises actual
RTT sampling, all three upload paths and halo readback on native KOS. Emulator
execution and hardware/performance evidence remain distinct.

Glow ray copies emit complete 8x6 mesh row packets so a centered light retains
its interior fade. The caller selects 1..6 copies and their ordered depths;
offscreen lights and zero weights emit nothing. A separate single-copy shaft
builder uses the same admitted map. This does not complete the independent
`sh4der_shafts` backend. Host tests check UVs, row boundaries, color variation
and transactional failures. Native readback checks center/corner variation and
compares half-weight shaft color with an independent halo quad using the same
texture/header modulation; it does not assume a particular RGB565 rounding rule.

## Reusable images and focus

`sh4der_kos_image.h` admits borrowed nonmip RGB565/ARGB1555/ARGB4444 surfaces
and compiles COPY(OP), ALPHA(TR), ADD(TR) and MULTIPLY(TR) headers once. Content
UVs choose edges or texel centers. Stride images retain the caller's existing
global-stride obligation. A complete quad builder reuses those headers; ordinary
meshes submit the selected header followed by all row strips through one sink.
The binding does not observe writes or retain readers. Ordered GPU consumption
uses existing scene ordering. CPU upload requires the destination's last use
COMPLETE, or explicit caller assurance of no uses; failed uploads prohibit any
old or new packet until complete replacement succeeds. No hidden registry or
allocator is introduced. Rebind on replacement or PVR restart.

`sh4der_kos_focus.h` prepares mask radii/reciprocals on the host or when a mask
changes, builds the408-vertex alpha mesh, and box-blurs an80x60 grid into caller
128x64 RGB565 storage with edge padding. The blur uses1440 bytes of automatic
row scratch. Static masks/meshes and images can be prepared on the host; dynamic
frames use retained sampling. Disc/band/rect masks use virtual640x480 coordinates
independent of the caller's output rectangle. Bind the blur image once as
LINEAR RGB565,80x60 content, bilinear, edge UVs and clamping. This is a stylized
focus mask, with no depth-buffer readback. `utils/sh4der-image-test` covers
independent blur arithmetic, prepared masks, header/lifetime failures and native
RGB565 rendering. Hardware behavior and cost remain separate.

## Warps

`sh4der_kos_warp.h` prepares static virtual320x240 grid coordinates once on the
host or at load, then uses caller-owned1312-byte row scratch for current ring,
haze or wobble sources. Public point/wave math is retained; per-vertex integer
grid division is removed. Bind the source image with texel centers and bilinear
filtering, then submit its COPY header and all returned row strips to one OP sink.
The image may be linear, twiddled or stride-backed; existing image lifetime and
global-stride contracts apply. Dynamic source admission precedes output, including
the SH4ZAM signed-truncation phase bound.

Radial blur builds1..4 complete packets: first COPY to OP, remaining ADD to TR,
with integer weights summing255 and distinct caller-selected reciprocal depths.
Pixelation emits300 independent quads with constant per-block UVs and uses a
NEAREST image binding. The dither helper uses the host-baked public8x8 ARGB4444
tile, a repeating TWIDDLED image binding and one MULTIPLY packet. No backend
allocates, submits, reads scanout or changes stride. Static output can be reused.
Native tests execute all paths including stride sampling and twiddled dither;
this establishes emulator rendering evidence, not physical hardware timing.

## Chromatic aberration and shadow shafts

`sh4der_kos_ca.h` precomputes viewport radial terms on the host or when the
viewport changes. Each update produces three complete channel packets in
GREEN/COPY(OP), RED/ADD(TR), BLUE/ADD(TR) order using admitted image UVs and
headers. No per-vertex divisions or implicit capture/allocation/stride changes.
The ordinary image quad supplies opaque/alpha frame copies. Rebuild the plan
and rebind images when viewport/storage changes, retaining all existing readers.

`sh4der_kos_shafts.h` implements shadow shafts separately from glow's additive
shaft copies. Prepare the4800-cell light-distance attenuation on the host for
static light, or when projected light position changes. Current map updates
reuse it without per-pixel roots, apply public darkness/cast math and half-step
temporal easing, and fill padding with white. Optional previous staging must
have an earlier render ID in the same generation; NULL resets to white. Always
use a pre-effect grid to avoid feedback. Upload via the admitted image helper
after retiring map readers, then submit the MULTIPLY header and108-vertex mesh
through one TR sink. Sample source identity does not retire map readers.
Native tests check exact zero-strength CA reconstruction, active color fringes,
shaft history attenuation and neutral reset; physical hardware remains separate.

## Lens and cached overlays

`sh4der_kos_overlay.h` compiles the four look blend modes plus alpha into caller
headers once. Complete quad output reuses these headers, with fog disabled and
explicit reciprocal depth/order. Other mesh consumers can submit one matching
header followed by all strips through the same existing sink.

`sh4der_kos_lens.h` bakes vignette geometry and deterministic64x64 ARGB4444 grain
on the host or at load. Vignette scratch is caller-owned; packed output is
reusable until shape/depth changes. Grain binding borrows the admitted image,
prepares amplitude/offset/mean compensation and compiles the offset-color header.
Per-frame grain packets change UV phase, then apply an ordered untextured BOOST
pass. Upload and reader retirement use the image contract. Unsupported mean
compensation above2x is explicit ERANGE, not silent clipping.

Public CPU hit_add/hit_tick and flash_hit/flash_step operate on caller-owned
state. Lens adapters produce visible hit wedges and additive/alpha flash packets
without mutating it. No global readiness flag, allocation, hidden grain/spoke
state or competing renderer is linked. Native tests exercise all lens paths;
physical hardware and performance remain separate validation levels.

## Visor modes

`sh4der_kos_visor.h` prepares OFF/NIGHT/THERMAL/AMBER presets, LUT, noise/scan
patterns and a base vignette mask on the host or when mode changes. Current
thermal conversion uses the retained pre-effect80x60 grid; output is edge-padded
128x64 RGB565. The caller binds/uploads thermal, grain and scan images and
retains all readers before updating them. No resources are implicitly allocated.

Ordered TR packets use cached overlay/image headers: grade or thermal first,
then optional grain and scanline passes. Grade, thermal and vignette use the
caller's fade amount; grain/scan strengths are baked as in pinned source and
are enabled for nonzero amount. NULL pattern images omit optional layers.
Vignette output reuses the prepared alpha mask without roots; bloom remains
the caller's existing glow pass at description.bloom*amount. Public visor_fade
state stays caller-owned. No per-frame preset fit, LUT bake or header compile.
Native tests cover all modes and exact neutral fade endpoints; no hardware or
performance claim follows from them.

## Torch

`sh4der_kos_torch.h` prepares fixed cone positions and band weights on the host
or when shape changes, using the pinned CPU mesh implementation. Current output
only repacks18 band-side colors and emits306 vertices, with separate MULTIPLY
and BOOST headers from the cached overlay. Daylight can retain the boost while
setting carve to zero. Public near_at composes with Compact's base_prepare hook.

The fog plan copies129 baseline entries and prepares their reach curve once.
Per-frame reach produces a caller-owned table. The renderer applies far depth,
BLACK fog color and the custom table at a safe scene boundary; this adapter
never writes fog state. The formula is not an equivalent tinted-fog operation.
Native checks cover neutral, dark-room and daylight cones plus near/far fog.
The CPU-only torch wrapper excludes the upstream GPU emitter and keeps pinned
SH4ZAM math on SH-4; it introduces no vendor edit or competing renderer.

## Retained trail and TAA

`sh4der_kos_trail.h` binds caller-owned RGB565 history images and publishes exact
producer tickets. Publication validates target/extent/generation once and accepts
pending GPU work under ordinary scene ordering; it is not CPU-read readiness.
Invalidate before overwrite, capture failure or a cut. A new generation and
rebind plus public camera reset are required on replacement/resize/PVR restart.

Trail/TAA produce keep and shifted ADD packets with cached headers. This uses
normal retained textures, so raw scanout parity masks, chip aliases, dither and
vertical-scale changes are unnecessary. Apply public TAA jitter to current scene
projection and keep the composited output as next history. Copy offsets use actual
output pixels; the adapter requires equal history/output extent. Exposed edges
retain the dimmed current frame, matching the pinned copy policy.

The caller retains both source and destination through all readers and producers.
Metadata invalidation cannot retire a texture. The packet builder rejects stale
identity and sampling the current destination. No allocator, hidden history or
extra scheduler is introduced. Native ping-pong tests cover recursive blending,
three motion copies, a cut and changed-generation resized history.

## Exposure and halo feedback

`sh4der_kos_look.h` prepares histogram bins/tuning once. Each retained grid is
consumed once in increasing render order within its generation. The eye removes
the sampled frame's recorded gain_ln, preserving adaptation state; it never
assumes the readback carries its most recent output. Missing/evicted metadata,
duplicate frames and generation mismatch leave state unchanged. Camera cuts and
resize reset the eye. Dynamic grade packets reuse cached overlay headers.

Glow feedback records bind a rendered frame to its immutable CPU map source and
actual summed big/small weights. Correction verifies both IDs, subtracts nearest
map estimates and removes recorded gain/darken. Multiple halo passes above1
are supported. Keep CPU staging until correction/discard; GPU reader retirement
is separate. Bilinear interpolation and clipping make this an estimate, as in
the pinned implementation. Arbitrary grades/ray transforms/other effects require
a pre-effect source. Corrected grids can feed exposure with gain1 metadata and
next glow preparation. Native tests exercise grade blend/quantization, adaptive
gain and composited halo correction; hardware behavior remains separate.

## AO authoring and screen cavity

`utils/sh4der-ao-test/cook.c` is an original host fixture for concave, flat and
convex authored edges. It sums each edge once per vertex and emits a retained
light sidecar indexed by sparse model indices4..7. `make test` regenerates and
byte-compares the checked-in header. Real asset tools can use the same public
crease helpers after deriving adjacency; no runtime crease discovery is needed.

`sh4der_kos_ao.h` turns a retained pre-effect80x60 RGB565 grid into a128x64
cavity multiply map using caller4800-byte luma scratch. It avoids a4800-float
intermediate and fills borders/padding with neutral white. This is a luminance
cue, not depth/normal SSAO. Bind/upload through image APIs, retain readers and
submit its MULTIPLY packet through the same TR sink. Native tests check an
original black/white boundary, neutral image and all three upload routes.

## Compact pose and receiver consumers

`utils/sh4der-compact-test/native.c` uses actual sparse indices4..7, reversed
strip input and repeated references. Skin sources/influences are admitted once;
changed palettes are prepared once per pose. `base_prepare` reads the admitted
pose index directly, applies lighting/near-torch, then the adapter composes
effect color and AO/limb attenuation. Root/child transforms and posed limb
centers share world space. A separately admitted child-owned skin verifies bone
self-exclusion; moving root/child tests space and refresh behavior. Projection
and XMTRX belong to the existing whole-batch renderer. No per-vertex matrix save.
The fixture also executes blade/holo and additive geometric bloom.

`foot-native.c` includes the host-cooked alpha mask, places a posed foot sphere
and refreshes its receiver plane before cone construction. It retains ordinary
receiver depth testing and disables shadow depth writes. Tests cover a near
occluder, height rejection and moved foot. The final render retires both RTT
producer and mask reader before release. Generate the mask on the host with
`sh4der-compact-test --cook-foot`; host tests byte-compare the checked-in header.

These native fixtures use original geometry/data and the pinned dependencies.
Rebuild the addon and consumers together after changes to public PVR context
layout or strip defaults; old ELF evidence does not validate a different base.
No upstream competing GPU lifecycle backend is linked.
