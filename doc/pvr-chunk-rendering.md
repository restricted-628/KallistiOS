# Compact-model rendering

The immediate and prepared compact-model emitters are bounded bridges between
admitted model streams and KOS PVR geometry sinks. They are deliberately
emitters, not a retained renderer: the application still owns the scene,
active list, materials, textures, model storage, animation state, preparation
memory, and workspace.

## Pipeline

One call performs these stages:

1. Preflight the complete polygon stream, matrix, workspace, sink, supported
   record families, state ranges, every referenced vertex, and total memory
   sink capacity.
2. Decode state records into `pvr_chunk_render_state_t`.
3. Resolve each indexed strip reference through the admitted model view.
4. Assemble one canonical or format-bound two-volume strip in caller-owned
   aligned workspace, correcting reversed-strip winding by exchanging the
   first two complete references.
5. Apply the explicit object-to-screen matrix through
   `pvr_geometry_project()` or `pvr_geometry_project_vertices()`. On Dreamcast
   this batch uses SH4ZAM while preserving the caller's XMTRX state.
6. Let the caller publish the strip's material, then emit the projected strip
   through the supplied memory, current-list, or explicit buffered-list sink.

No heap allocation, worker thread, hidden model index, or global render state
is introduced. The workspace only needs one vertex per entry in the largest
strip and can be reused as soon as the call returns.

The immediate functions scan bounded vertex records for every indexed
reference and require no preparation storage. Their `_prepared` counterparts
use `pvr_chunk_model_plan_t` to resolve the same references through a sparse
page table in constant time. Both forms preserve identical state, callback,
projection, sink, progress, and error behavior. Workspace and memory output
must not overlap either the prepared plan or its borrowed index entries.

## Material and texture boundary

Compact texture records contain an asset identifier and sampling intent, not a
complete KOS texture surface. They do not specify a VRAM pointer, dimensions,
pixel format, mip layout, or ownership. Guessing those fields would make model
data control unrelated memory.

The `pvr_chunk_render_begin_strip_t` callback therefore receives the complete
decoded state immediately before each strip is emitted. For a PVR list sink,
the callback is required and normally performs these application-owned steps:

- resolve the outside and, when present, inside texture identifiers in an
  asset table;
- build or select the matching ordinary or two-volume polygon contexts;
- apply the decoded blend, filter, clamp, flip, mipmap, color, and strip policy;
- compile or select a checked `pvr_material_t` with
  `pvr_material_compile()` or `pvr_material_compile_two_volume()`; and
- submit the material to the same current or buffered list as the geometry.

Memory sinks may omit the callback because no polygon header is submitted.
They can also use it to capture state beside the emitted vertex array.

## Default vertex policy

The emitter supplies deterministic defaults before
`pvr_chunk_render_prepare_vertex_t` runs:

- position comes from the indexed vertex record;
- UV0 comes from the strip reference when present, otherwise zero;
- strip ARGB overrides vertex diffuse ARGB, which overrides material diffuse,
  with opaque white as the final fallback;
- vertex specular ARGB overrides material specular, with zero as the fallback;
  and
- the last vertex in every strip receives `PVR_CMD_VERTEX_EOL`.

The prepare callback may replace every non-command vertex field. It receives
decoded normals, intensity values, metadata, user data, and bounded triangle
user words. A callback is required if any referenced vertex uses intensity
fields because combining those values with material and lighting state is
application policy rather than a universally correct packed-color conversion.
It is also required for a position W other than one because canonical
`pvr_vertex_t` carries only XYZ; the callback must resolve that extra component
into the submitted position. The emitter restores the command field after the
callback.

## Environment-map policy

`pvr_environment_map_uv()` converts one normalized view-space normal into the
standard KOS top-left-origin sphere-map coordinates. The allocation-free
`pvr_chunk_environment_map_binding_t` adapter makes that kernel directly
usable with the ordinary compact-model emitter while preserving its two
callbacks and one opaque-data argument.

The adapter builds an inverse-transpose normal matrix from the caller's
object-to-view transform. On a strip carrying
`PVR_CHUNK_STRIP_ENVIRONMENT`, it prefers the per-reference strip normal over
the indexed vertex normal, transforms and normalizes it, and replaces UV0.
This precedence preserves authored hard-normal seams. It then invokes an
optional chained vertex callback, which may override the coordinates or add
lighting and other application policy. The required chained begin-strip
callback continues to own material resolution and header submission.

The generic UV kernel is also directly usable from two-volume and prepared
draw-cache callbacks. Those paths retain their distinct vertex layouts and do
not acquire hidden adapter state. Missing normals report `ENOTSUP`; malformed
normals and transforms report checked domain/range errors instead of silently
using the model's stored UV.

## Extended lighting policy

`pvr_lighting_apply_extended()` produces the two packed colors consumed by an
ordinary PVR vertex. Diffuse lighting, including negative-intensity dark
lights, is accumulated before saturation and multiplied by each sample's
diffuse material color. Positive lights can separately generate Blinn-Phong
RGB for `oargb`; the offset color's ignored alpha byte remains zero.

The optional distance cue linearly interpolates two caller-selected factors
over a world-space view-distance interval and multiplies only diffuse alpha.
It therefore composes with application blending and fog choices instead of
turning either one into model-owned state. Compact immediate and draw-cache
callbacks can build samples from decoded or deformed position/normal data and
write the returned `argb` and `oargb` directly. The original minimal Lambert
function remains available when none of this policy is required.

`pvr_chunk_render_policy_binding_t` supplies the standard immediate/prepared
emission adapter for that work. Its three presets are deliberately small:

- UNLIT preserves decoded colors and applies optional vertex intensities;
- DIFFUSE adds ambient plus signed directional or point Lambert lighting; and
- DIFFUSE_SPECULAR additionally generates positive-light offset color.

Environment-map UV generation and distance-cue alpha are orthogonal features.
The binding canonicalizes homogeneous positions, prefers per-reference normals
over indexed normals, maps compact exponent 0 through 16 to lighting powers 1
through 17, observes the strip ignore-light/ambient/specular flags, and then
runs an optional application callback. Its required begin-strip callback still
owns material/header submission. Matrices and lighting context are copied at
initialization; the admitted light array remains borrowed and immutable.

This is a rendering-policy adapter, not a second renderer. It adds no model
records or render-function markers, begins no scene or list, retains no model,
allocates nothing, and performs no work when not selected. Applications can
still pass their own callbacks directly to every emitter and cache path.

## UV encoding policy

The compact stream has three signed UV encodings with an explicit density and
range order. Signed UV10 is the default: it uses 6.10 fixed point for roughly
`[-32, 32)` with 1/1024 steps. Signed UV8 uses 8.8 fixed point for roughly
`[-128, 128)` with 1/256 steps when wider tiling is required. A finite
floating-point record is the escape path only when a coordinate lies outside
both fixed-point ranges. It is never selected merely to replace the normal
compact quantization policy.

The host converter selects that order automatically and never clamps an
out-of-range coordinate. A floating-point texture set occupies four 16-bit
words per vertex instead of two, which keeps the larger representation a rare
and measurable content choice. All three forms decode to canonical floating
U/V before clipping, cel-band subdivision, environment mapping, or prepared
cache construction, so later rendering policy does not branch on storage
format. Existing unsigned records keep their original meanings; the signed
and floating encodings have distinct record identifiers.

Topology-changing cel shading uses the same separation. The low-level
`pvr_toon_split_triangle()` primitive accepts an already evaluated scalar on
each canonical working vertex and partitions the triangle across any number of
ordered thresholds. The single-threshold case produces the exact sharp binary
boundary; additional thresholds use the same implementation. Signed UV8 and
signed UV10 records have already decoded to floating U/V before this stage, so
generated boundaries interpolate one canonical representation. The primitive
owns no material, light collection, model, cache, list, or scene.

`pvr_chunk_model_cache_emit_toon()` is the prepared hot path over that
primitive. It resolves the current deformation pose, batch-transforms normals
through the caller's inverse-transpose matrix, evaluates one scalar per strip
reference, restores triangle-strip winding, and subdivides at every crossed
threshold. The resulting independent triangles are color-modulated per band
and then pass through the existing SPLIT, DROP, or ASSUME_VISIBLE frustum
policy. Doing band subdivision before frustum clipping preserves generated UV,
base-color, and offset-color continuity through both topology-changing stages.

One threshold is the exact binary profile; multiple thresholds use the same
bounded implementation. An optional profile resolver can select different
thresholds and modulation ramps per cached material state. Flat-shaded strips
use one geometric face normal and therefore acquire no false internal
boundary. IGNORE_LIGHT strips remain unmodified. The cache, profile arrays,
strip scratch, maximum `2*N+1` band-triangle scratch, clip scratch, scene,
material header, and sink are all caller-owned. There is no per-frame
allocation, retained light manager, renderer marker in the model stream, or
mandatory cost for applications that do not call this policy.

`pvr_chunk_model_two_volume_cache_emit_toon()` applies the same geometric
policy to prepared two-volume surfaces without collapsing their paired
attributes. Band subdivision interpolates both UV pairs, both diffuse colors,
and both offset colors. Outside- and inside-volume colors have independent
optional modulation ramps. Intersecting geometry is clipped by two parallel
canonical attribute passes with identical positions, then recombined into the
original 32-byte color or 64-byte textured packet. The hardware therefore
continues to select the outside or inside parameter set per pixel after both
topology-changing stages. IGNORE_LIGHT preserves both authored sets unchanged.

For content that does not need topology-exact boundaries,
`pvr_toon_ramp_init()` admits a small borrowed lookup ramp once and
`pvr_toon_ramp_apply()` performs a logarithmic per-vertex lookup without
revalidating it. Base and offset colors have independent optional modulation
arrays, so a compact renderer can quantize diffuse bands and highlight bands
without allocating storage or changing the model grammar. This is explicitly
a throughput policy: endpoint colors may be interpolated across a primitive.
The prepared geometric emitters remain the exact choice when a threshold must
become a sharp line inside a triangle.

Prepared-cache wireframe drawing is another policy over the same canonical
geometry. `pvr_chunk_model_cache_emit_wire()` enumerates unique edges directly
from triangle-strip references instead of emitting all three edges of every
independent triangle. The full mesh mode therefore emits `2*N-3` edges for an
N-reference strip; boundary mode emits the N outside edges, and path mode emits
the `N-1` consecutive-reference edges.

Each centerline segment is clipped against the homogeneous screen/W frustum
before it becomes a four-vertex screen-space quad. This preserves a constant
pixel width and prevents a near-plane endpoint from reaching projection. A
profile may preserve prepared/per-frame endpoint colors or replace them with a
constant base and offset color. The model contains no wireframe marker, and the
cache, resolved-strip workspace, profile, material header, list, and scene all
remain caller-owned.

Silhouette outlines use a separate inverted-shell pass rather than changing
the compact model grammar. `pvr_toon_outline_expand()` provides checked normal
extrusion, using SH4ZAM vector math on Dreamcast, while
`pvr_chunk_model_cache_emit_outline()` composes that operation with prepared
deformation, smooth or flat-shaded normal policy, frustum clipping, and the
existing geometry sinks. Applications draw this shell before the ordinary
surface and submit an untextured material with the opposite culling mode.
Object-space width, colors, scratch, material, scene, and list remain explicit.

The dedicated two-volume path follows the same position and callback policy.
Ordinary texture and material records update the outside-volume fields;
two-volume variants update `secondary_*`, the inside-volume fields. Textured
two-volume strips carry independent UV0 and UV1. Vertex packed colors override
the corresponding material color in both parameter sets; otherwise each set
uses its own material diffuse/specular value, then opaque white/zero. The
untextured 32-byte layout has two diffuse colors and no offset-color fields.

## Preflight and failure

Built-in validation finishes before the first callback or sink write. A memory
sink also reserves capacity for the complete model up front, so `ENOSPC` cannot
leave a partial model there. Reserved state bits and invalid exponents report
`EILSEQ`; arithmetic overflow reports `ERANGE`.

Callbacks, projection, and non-memory sink publication can still fail after a
prior strip was emitted. `pvr_chunk_render_result_t` and the sink's emitted
counter then describe the complete valid prefix. A callback returning a
negative value should set errno; the emitter uses `EIO` if it did not.

The ordinary compact-model path intentionally fails with `ENOTSUP` during
preflight for record families that need a different material or topology
contract:

- modifier-volume geometry;
- two-volume texture, material, and strip records;
- cached-polygon control records.

These records are not skipped or approximated. Deferred polygon capture/draw
controls are classified by `pvr_chunk_model_info_t` as requiring polygon
canonicalization; they describe cross-hierarchy execution ordering rather than
a reusable submesh inside one compact model. Importers must evaluate their
complete deformation dependency and emit ordinary strips. KOS deliberately
does not create a second runtime polygon cache beside prepared draw caches.

Two-volume texture, material,
and strip records instead use `pvr_chunk_model_emit_two_volume()`, which binds
the entire call to either KOS's complete 32-byte untextured layout or 64-byte
textured layout and rejects mixed layouts before output. Modifier geometry,
and cached-polygon controls remain explicit unsupported boundaries in both
polygon emitters.

An ordinary bump-material record updates a persistent signed-normalized
direction/up basis in `pvr_chunk_render_state_t`. The ordinary emitter requires
a prepare callback for every later strip while that basis is active. That
callback combines the basis with application-owned light direction and bump
strength, then writes the PVR packed bump value to the vertex offset color.
The renderer does not guess either missing input. Two-volume emission rejects
bump material because the compact format defines no corresponding inside
parameter record.

## Frustum policy

`pvr_chunk_model_emit_clipped()` and its prepared-plan variant make clipping a
per-call choice for ordinary compact strips. Both first classify the retained
model sphere. An outside model returns before touching either stream, while an
inside model stays on the ordinary strip emitter. An intersecting model is
expanded into independent winding-correct triangles under one of three
policies:

- `PVR_CHUNK_CLIP_SPLIT` clips against all six homogeneous planes and linearly
  interpolates UV, base color, and offset color at generated edges;
- `PVR_CHUNK_CLIP_DROP` emits only triangles already wholly inside every plane;
- `PVR_CHUNK_CLIP_ASSUME_VISIBLE` skips classification and clipping when the
  caller has stronger scene-level knowledge.

The split workspace holds the bounded output of one source triangle; it is not
a model-sized intermediate buffer. Memory sinks are preflighted against the
worst-case expansion before callbacks or output, so insufficient capacity is
failure-atomic. The retained sphere must enclose any callback-driven
deformation used to make the model-level rejection safe.

Two-volume vertices carry more interpolants than canonical one-volume packets,
so that path remains separate rather than silently discarding its extra state
through the ordinary clipper. Closed modifier volumes use the dedicated
near-plane warp described below.

## Modifier-volume topology

`pvr_chunk_model_emit_modifiers()` is a separate pass over the same admitted
model view. It consumes only modifier-volume records and therefore composes
with either polygon emitter without duplicating model storage. Triangle records
emit directly. Quads split into `(0,1,2)` and `(2,1,3)`. Strip expansion swaps
the first two indices on alternating triangles, and also applies the record's
reversal flag, so independent modifier packets preserve strip winding.

Each nonempty volume record is one logical modifier volume. Every triangle but
the last receives `PVR_MODIFIER_OTHER_POLY`; the last receives the caller's
validated include or exclude mode. List, culling, and final-mode policy are
explicit in `pvr_chunk_modifier_config_t` because the compact topology record
does not own PVR list policy. For current or buffered output, each checked
32-byte header and 64-byte triangle is published as one 96-byte operation.
Memory sinks receive only projected `pvr_modifier_vol_t` packets.

The optional callback receives decoded source vertices and bounded per-triangle
user words. It can apply an application-specific position-W policy or fill
dummy fields before all three positions are projected through the format-aware
SH4ZAM geometry path. Complete triangle count, memory capacity, configuration,
and overlap validation occur before callbacks or output.

`pvr_chunk_model_emit_modifiers_warped()` and its prepared-plan variant solve
the modifier near-plane case without cutting open the source volume. They use
the transform and near-W bound in a caller-owned `pvr_frustum_t`; every point
nearer than that bound is projected on the bound instead. Shared source points
therefore remain shared, the triangle count and include-last sequence do not
change, and the volume cannot acquire a clipping hole. Side and far clipping
remain normal PVR policy. The unwarped entry points remain the lowest-overhead
choice when scene-level bounds prove the volume does not cross the near plane.

## Concurrency and lifetime

The model view and both source streams must remain immutable for the complete
call. Workspace may not overlap either stream, the transform matrix, or a
memory sink; a memory sink must also remain separate from the streams and
matrix. Callbacks execute synchronously on the caller's thread. They may use
application asset tables, but must not mutate the active model, workspace, or
sink. The emitter retains no pointer after return.

## Remaining compact-model work

The prepared ordinary band-shading path closes the highest-priority cel
topology gap, but it does not make the broader compact-model program complete.
The remaining work is deliberately tracked here so renderer work cannot hide
format, deformation, or tooling gaps:

1. Define a backward-compatible section-directory asset revision when compact
   assets need to bundle optional hierarchy, general-skin, morph, animation,
   collision-volume, resource-table, or cooked-cache sections. PCM1 remains
   the small two-stream container; a larger fixed header must not grow around
   every optional feature.
2. Extend the host compiler around one canonical scene IR, including modern
   scene import, lossless general skin and morph import, hierarchy/evaluation
   metadata, parent-result canonicalization, elimination of deferred polygon
   controls, optional cooked caches, and
   repeatable round-trip conformance fixtures for seams and deformation.
3. Add exact imported hierarchy policies only where conversion proves they are
   observable: translation/rotation/scale suppression, child-pruning, and
   explicit XYZ/ZXY Euler sampling beside the preferred quaternion path. Do
   not replace the current flat parent-before-child hierarchy or complete-pose
   deformation with pointer trees or deferred polygon execution.
4. Extend the separate cell-sprite layer with host authoring support. Its core
   timestamped stream/list sampling and events, generic whole-motion binding,
   signed priority, material/color metadata, and compact or colored 2D/3D
   geometry paths are complete and remain outside the 3D compact mesh grammar.

Already completed and not to be reimplemented are distinct signed UV8/UV10
records with automatic host-side selection and a finite float-UV escape for
coordinates outside both compact ranges, variable-count skin influences
beside the four-weight fast path, shape-to-morph binding, volume
iteration/collision reuse, retained bounds and clipping, environment-map UV
generation, signed diffuse/specular lighting, Catmull-Rom tracks, hierarchy
traversal, ordinary/two-volume/modifier prepared caches, prepared-cache
inverted-shell outlines, and strip-topology wireframe drawing.

The signed encodings use distinct record types, so existing unsigned streams
remain unambiguous without adding a version field to the borrowed raw-model
descriptor. Container versioning, optional bundled sections, and stream-record
semantics remain separate contracts.
