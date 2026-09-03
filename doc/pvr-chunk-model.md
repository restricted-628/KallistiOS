# Compact PVR model streams

KOS compact models separate vertex data from polygon state. Vertex records use
32-bit words; polygon records use 16-bit words. Both streams are explicitly
bounded and end with a terminal record, so consumers never have to discover a
length by reading past caller-owned memory.

`pvr_chunk_model_validate()` is the admission boundary. It checks the complete
model without allocating memory:

- record classes, lengths, and exact terminal placement;
- the word stride and entry count of every vertex and shape format;
- finite position, normal, center, and radius values;
- unique vertex-index ranges, reserved packed-normal bits, and encoded UV
  ranges;
- material payload lengths;
- strip, triangle, quad, and modifier-volume framing;
- overflow-safe aggregate counts; and
- every polygon index against the ranges actually defined by vertex records.

Validation intentionally does not mutate or retain the streams. Applications
can keep compact data in read-only storage, map it from an asset container, or
construct it in caller-owned memory. `pvr_chunk_iterator_next()` provides a
safe allocation-free view of each complete record for inspection and tooling.

`pvr_chunk_model_open()` combines that admission check with an immutable model
view and cached summary. The source words remain caller-owned and must not be
modified while the view is in use.

## Typed stream views

After validation, callers do not need to reproduce the stream's internal size
arithmetic:

- `pvr_chunk_vertex_batch_decode()` exposes the first index, entry count,
  format, and exact per-entry word stride of a vertex record;
- `pvr_chunk_vertex_batch_get()` returns one bounded entry with a decoded
  finite XYZ or XYZW position and the original raw attribute words;
- `pvr_chunk_strip_iterator_init()` and `pvr_chunk_strip_iterator_next()` walk
  the separately framed strips inside one polygon record; and
- `pvr_chunk_strip_vertex_get()` resolves one indexed reference, its format
  words, and the optional per-triangle user words that follow the vertex which
  completes that triangle.

`pvr_chunk_vertex_attributes_get()` expands recognized vertex normals, packed
colors, intensities, user data, and generic metadata into a format-neutral
value. The generic metadata field is deliberately not treated as a complete
skinning influence; applications that use it for model-specific deformation
must supply an explicit interpretation. The corresponding strip decoder
retains legacy unsigned-normalized UV records and decodes the preferred signed
8.8 and 6.10 fixed-point records. Signed records provide approximately
`[-128, 128)` at `1 / 256` precision or `[-32, 32)` at `1 / 1024` precision,
including negative and repeated coordinates. The decoder also expands signed
normals and ARGB color and retains bounded per-triangle user words.

`pvr_chunk_model_vertex_attributes_get()` resolves an index directly from an
admitted model without allocating memory. Admission rejects overlapping
vertex ranges, so the answer is deterministic. The lookup scans bounded
records and remains the zero-preparation path.

## Prepared vertex plans

Frequently rendered models can replace repeated record scans with an optional
caller-owned `pvr_chunk_model_plan_t`. `pvr_chunk_model_plan_query()` reports
the exact index storage before any caller buffer is modified, and
`pvr_chunk_model_plan_build()` admits the complete model again before
publishing the plan.

The direct index divides the 16-bit vertex namespace into 256 pages of 256
indices. Only pages containing defined vertices consume caller storage. Lookup
therefore remains constant-time without imposing a flat 65,536-entry table on
a sparse model. Each page entry records the bounded source-word offset and
vertex format needed to decode that one index. Missing pages and holes report
`ENOENT`; malformed or modified plan metadata reports `EILSEQ`.

The plan allocates nothing and retains no hidden state. It borrows its compact
entry array and copies the admitted model view. The source streams, index
entries, and plan must remain immutable and at their original addresses while
the plan is used. Applications that draw a model rarely can continue using the
immediate view and pay no index-storage cost.

Both decoded forms retain access to the raw bounded views, allowing uncommon
application policy without weakening the checked framing boundary.

## Explicit skinning

`pvr_chunk_skin_bind()` associates one compact influence record with every
vertex in a prepared model. Records are strictly ordered by vertex index and
carry four 16-bit joint indices plus four unsigned-normalized weights. Active
weights must sum to exactly 65535, inactive slots are canonical zeroes, every
joint must fit the declared palette, and every admitted model vertex must be
covered exactly once. This replaces draw-order-dependent accumulation with a
deterministic, byte-comparable side table.

Binding completely validates the model-to-influence relationship before it
writes the caller-owned sparse-page lookup. `pvr_chunk_skin_source_build()`
then decodes positions, normals, and floating influences once into an exact
32-byte-aligned caller workspace. The resulting source feeds
`pvr_chunk_skin_apply()` for each sampled joint palette without reparsing the
compact vertex stream. The operation uses the existing bounded deformation
kernel and its SH4ZAM target path.

A completed `pvr_chunk_skin_pose_t` retains the binding and dense deformed
array. `pvr_chunk_skin_pose_vertex_get()` maps the original 16-bit model index
to its pose vertex in constant time, so render policy callbacks can replace
position and consume the deformed normal without creating a global vertex
buffer. Applications that do not bind a skin link no additional state and
allocate no lookup, source, or pose storage.

Skin4 remains the compact and optimized common path, but it is no longer a
representation ceiling. `pvr_chunk_skin_general_t` stores one ordered span per
model vertex plus a packed unsigned-normalized joint/weight array. Spans may
contain any count representable by their 16-bit length, must pack without
gaps, and must each sum exactly to 65535. The general binding and source stages
retain the same complete validation, sparse-page lookup, caller-owned storage,
and pose-resolution contracts as Skin4, then feed
`pvr_skin_apply_spans()`. A host compiler can therefore reduce a skin to four
weights when the error budget permits it and preserve every influence when it
does not, without introducing draw-order-dependent accumulation.

`pvr_deform_bounds_calculate()` closes the culling contract after either
morphing or skinning. It scans any strided canonical deformation stream once,
publishes its exact current-pose AABB, and derives a conservative sphere around
that box. The sphere can be passed directly to
`pvr_frustum_classify_sphere()` before rendering; it may be looser than a
minimum enclosing sphere, but cannot reject visible deformed geometry. The
operation allocates nothing, ignores normals, and leaves output unchanged on
empty, malformed, overflowing, or nonfinite input.

PCM2 can carry that same representation in a pointer-free little-endian `PSG1`
section. `pvr_chunk_skin_general_section_open()` verifies its framing,
checksums, reserved fields, ordered gapless spans, joint bounds, nonzero
weights, and exact normalized totals before publishing a borrowed view. The
indexed accessors decode individual records, while
`pvr_chunk_skin_general_section_materialize()` fills caller-owned aligned span
and weight arrays only after capacities and overlaps are completely checked.
The resulting `pvr_chunk_skin_general_t` goes through the existing model-aware
query and bind path; the section parser does not duplicate or weaken that
authority. Applications that do not load a skin section allocate nothing.

An optional pointer-free `PSK1` skeleton section completes the authored skin
contract without embedding target pointers. Each fixed record binds one skin
joint ordinal to one unique hierarchy-node ordinal and carries its finite
column-major inverse-bind matrix. The parser validates both CRCs, exact sizes,
reserved bytes, node bounds, duplicate bindings, and every matrix component
before publishing a borrowed view. Materialization fills caller-owned joint
records only after all capacities, alignments, and overlaps are preflighted.

`pvr_chunk_skeleton_palette_build()` composes each completed hierarchy world
matrix with its inverse bind and derives the matching inverse-transpose normal
matrix. It proves every input and every nonsingular result in a first pass,
then publishes both caller-owned arrays in a second pass. This directly feeds
the existing Skin4 or general-N deformation kernels; no skeleton object,
temporary palette, worker, or allocator is hidden inside the PVR runtime.

## Explicit shape motion

`pvr_chunk_shape_bind()` associates one or more sparse morph targets with a
prepared model. Each target is a strictly ordered set of canonical position
and normal deltas keyed by the original 16-bit model vertex index. Binding
re-admits the model, validates the caller-owned plan, rejects duplicate or
absent indices and non-finite deltas, and proves every target-to-base
relationship before modifying the caller's sparse-page lookup.

`pvr_chunk_shape_source_build()` decodes the base vertices once in ascending
index order and expands each sparse target into a target-major dense array in
one exact, 32-byte-aligned caller workspace. Missing records become zero
deltas. `pvr_chunk_shape_motion_bind()` then pairs each dense target with an
optional scalar animation track and fallback weight. Its output feeds
`anim_morph_targets_sample()`, followed by `pvr_chunk_shape_apply()` and the
existing bounded morph kernel. The complete per-frame path is:

```
sample scalar channels -> apply dense morph targets -> resolve pose -> emit
```

`pvr_chunk_shape_apply()` checks that sampled targets retain their original
dense pointer and ordering before publishing output. A completed
`pvr_chunk_shape_pose_t` resolves original model indices in constant time for
the immediate or cached deformation callback. The shape layer allocates
nothing, owns no playback clock, and is completely absent from applications
that do not bind shape data.

PCM2 stores the same sparse representation in a pointer-free little-endian
`PMS1` section. Each target names one gapless span in a packed delta array;
each delta stores a 16-bit model vertex index and finite XYZ position/normal
changes, with canonical zero W components reconstructed by the loader.
`pvr_chunk_shape_section_open()` checks framing, checksums, ordered target
spans, finite records, and strictly increasing indices before publishing a
borrowed view. Indexed accessors decode individual spans and deltas, while
`pvr_chunk_shape_section_materialize()` fills caller-owned target and delta
arrays only after complete capacity, alignment, and overlap validation. The
result still passes through `pvr_chunk_shape_bind()`, which remains the sole
authority for proving every sparse index exists in the selected model.

Scene-instance morph weights are stored separately in pointer-free
little-endian `PMW1`. Ordered bindings name a hierarchy node and its model,
then own one finite STEP, LINEAR, or explicit cubic Hermite scalar channel per
target. Admission checks
gapless channel and key spans, strict key times, CRCs, and the exact aggregate
interval. `pvr_chunk_morph_animation_section_validate_scene()` proves each
node/model association against `PCH1` and `PMT1`, then proves the referenced
`PMS1` target count matches the binding. Materialization fills caller-owned
`anim_scalar_hermite_key_t`, `anim_track_view_t`, and
`pvr_chunk_shape_channel_t` arrays, so the existing shape-motion sampler and
deformation kernel remain the only runtime path.

## Versioned asset containers and optional compression

`pvr_chunk_asset_open()` admits a bounded, versioned container whose vertex
and polygon streams are separate 32-byte-aligned sections. Each section has an
independent stored size, decoded size, codec, dictionary identifier, and CRC32.
The fixed header has its own CRC and reserved-zero fields, and every span is
checked for overflow, overlap, and natural word alignment before it is exposed.

Raw sections are borrowed directly when the container address permits it. A
compressed or unaligned section is materialized into exact caller-owned
workspace, CRC-checked, and then passed through the normal complete compact-
model admission boundary. The core container API allocates nothing and does
not create a thread, fiber, callback worker, or decompression dependency.

PCM2 preserves that contract while replacing header growth with a fixed
64-byte header and a checksummed directory of 32-byte descriptors. At least
one vertex and one polygon stream are required. Equal type-local ordinals form
the legacy implicit pairs used by `pvr_chunk_asset_model_workspace_query()`
and `pvr_chunk_asset_model_load()`. The explicit pair operations can instead
join any admitted vertex and polygon ordinals, allowing canonical draw-order
segments to share one immutable vertex stream. The original workspace and
load calls remain ordinal-zero wrappers. The container's sphere is
conservative for every model, so multi-model loading is correct even before a
pipeline supplies narrower per-model metadata. PCM1 exposes one model through
the same contract.

Host-produced PCM2 files additionally carry one pointer-free `PMT1` model
table. Its fixed 64-byte record maps each model ordinal to required vertex and
polygon streams, an exact finite local sphere, and zero-based ordinals for
that model's optional resource, volume, compact skin, general skin, skeleton,
morph, and cooked-cache sections. Version two selects the two required streams
independently and admits shared streams; version one remains readable with its
original equal record/stream ordinal rule. `UINT32_MAX` denotes an absent
optional section. Header and payload CRCs, reserved fields, bounds, and every
referenced section type are admitted before a record can be used.
`pvr_chunk_model_table_workspace_query()` and
`pvr_chunk_model_table_load()` remain allocation-free and replace the
container-wide conservative sphere with the selected model's exact bounds.

Resource tables, volume data, compact or general skins, skeleton bindings,
morph targets, hierarchies, animations, cooked caches, and application-defined
sections are optional; repeatable types are addressed by zero-based ordinal
rather than by adding pointers to the fixed asset view. Unknown nonzero section
identifiers remain queryable for forward compatibility.

Volume data uses a pointer-free little-endian `PVL1` section containing exact
compact triangle, quad, or strip volume records. The descriptor table only
names gapless record spans; it does not flatten topology into a second
collision format. `pvr_chunk_volume_section_open()` checks the section CRC,
record boundaries, and every payload before publication.
`pvr_chunk_volume_section_record_get()` returns an ordinary
`pvr_chunk_record_t`, and the flattened section iterator delegates each record
to the existing `pvr_chunk_volume_iterator_t`. Consequently collision queries,
modifier preparation, winding rules, record boundaries, and zero-to-three
triangle user words retain one implementation. A standalone section can be
checked against an admitted model with
`pvr_chunk_volume_section_validate_model()`, which rejects every unresolved
vertex index before the application uses the data.

Directory descriptors are ordered by increasing stored-data offset. Admission
proves the complete directory checksum, each section span and codec, decoded
alignment, raw-size identity, and nonoverlap before publishing either core
stream. `pvr_chunk_asset_section_get()` and
`pvr_chunk_asset_section_find()` return checked borrowed views. The matching
workspace query and load APIs preserve zero-copy access for aligned raw bytes
and CRC-check either borrowed or caller-decoded data. PCM1 is exposed through
the same APIs as two synthetic sections, so consumers can migrate without a
second loading path.

The model converter continues emitting PCM1 by default because it is smaller
for a model with only two streams. `--section-directory` emits an admitted
PCM2 container when a content pipeline intends to append optional sections;
it composes with raw or LZ4-framed vertex storage and always writes the `PMT1`
model table. The converter reopens, cross-validates, and loads either output
through the runtime parser before publishing the file.
When PCM2 input polygon data contains compact volume records, the converter
also writes a `PVL1` volume-data section automatically. The host serializer
copies the admitted records exactly, and the converter reloads the resulting
section and binds every expanded triangle back to the model before publishing
the file. Models without volume records gain no section and pay no file or
runtime-memory cost.

For glTF/GLB input, base-color images referenced by imported materials are
compiled into one pointer-free `PTX1` texture-image section. The host tool
decodes external, buffer-view, or base64 image sources, requires exact
power-of-two dimensions from 8 through 1024, selects RGB565 for opaque images,
ARGB1555 for binary alpha, or ARGB4444 for graduated alpha, and writes
pre-twiddled 16-bit storage bytes. Each stable texture ordinal has independent
metadata and a payload CRC; the section also checksums its entry table, full
data span, and header. Images not referenced as base color are not retained,
and an explicit `--texture-id` continues to mean application-owned texture
content, so neither case adds a section.

`pvr_chunk_texture_section_open()` admits the complete section without
allocation. Indexed and identifier lookups return borrowed immutable image
views. `pvr_chunk_texture_image_surface_init()` derives an unbound checked
surface, after which the application may allocate or bind VRAM and use
`pvr_chunk_texture_image_upload()` with the normal CPU, store-queue, or DMA
transfer policy. This closes image compilation and binding without making the
model container own VRAM, residency, transfer workers, or frame lifetime. VQ,
mip generation, palette assignment, resizing, and non-base-color material
roles remain explicit host-pipeline extensions rather than silent conversion.

Authored glTF material opacity is retained as renderable compact state.
`OPAQUE` materials use one/zero blending, `MASK` materials retain alpha and
route to the punch-through list, and `BLEND` materials use source-alpha/
inverse-source-alpha blending and route to the translucent list. `doubleSided`
sets the compact double-sided strip policy. Because the hardware exposes one
global punch-through threshold rather than a per-material threshold, imported
`MASK` materials currently require the glTF default cutoff of 0.5; applications
should set `pvr_set_punch_through_alpha(128)` for that pass. MTL `d` and `Tr`
values are likewise preserved, with partial opacity using the translucent
route. Unsupported alpha contracts fail conversion rather than silently
changing authored semantics.

The importer lowers authored triangle, triangle-strip, and triangle-fan
primitives to the same canonical triangle stream. Alternating strip winding is
resolved before the ordinary order-preserving strip optimizer runs. A base-
color texture may select any present texture-coordinate set, and its finite
offset, rotation, and nonuniform scale are baked on the host before signed
UV8/UV10 or floating-point selection. Neither feature adds a target topology,
texture-transform object, scene dependency, or per-frame calculation.

With `--scene-root`, the host-side canonical scene IR writes one `PCH1`
hierarchy section containing an identity root bound to model ordinal zero.
The converter then loads, admits, and binds that section through the target
implementation before publishing the PCM2 file. The OBJ boundary has only one
model and therefore emits only this explicit root; the glTF/GLB importer
populates the same IR for an authored selected scene rather than defining
another target format.
Every unique mesh in that selected scene becomes a separate PCM2 model
ordinal; repeated instances share the ordinal, while hierarchy nodes retain
their distinct local transforms. Each model receives exact `PMT1` bounds and
its own optional resource-manifest, cooked-cache, general-skin, skeleton, and
morph ordinals. Multi-model raw and LZ4-framed outputs are reloaded with
persistent nonoverlapping decode workspace before publication. Each optional
section is independently serialized, assigned, materialized, and checked for
its owning model; a missing section on one model does not shift another
model's type-local ordinal.
With `--rigid-skin`, the same host pipeline writes one `PSG1` section whose
vertices are each fully weighted to joint zero, then reloads and materializes
it through the target implementation before publishing. This deliberately
tests the section contract without claiming that the bounded OBJ source can
express an authored rig. When paired with `--scene-root`, it also emits an
identity `PSK1` joint binding. The glTF/GLB importer supplies exact general-N
spans, weights, hierarchy-node bindings, and inverse-bind matrices to those
same serializers.
With `--morph-target DX DY DZ`, it writes one `PMS1` section containing a
single position delta for model vertex zero, then reloads and materializes it
through the target implementation. This gives bounded OBJ builds an explicit
round-trip fixture without inventing morph semantics that OBJ cannot express;
the glTF/GLB importer populates the same sparse target representation.

With `--animation-offset DX DY DZ`, the converter writes one pointer-free
`PAT1` section containing a two-key linear translation track for the explicit
scene root. The section carries a finite clip interval, canonical scalar,
vector, quaternion, or Boolean keys, typed channel ordinals, transform
fallbacks, rotation modes, and visibility fallback state.
`pvr_chunk_animation_section_open()` admits the whole section before
publication; indexed accessors expose its transform and track records, and
`pvr_chunk_animation_section_materialize()` fills caller-owned canonical keys
and existing animation runtime bindings. Version 1 remains readable with
quaternion-only rotation; version 2 retains quaternion, XYZ Euler, or ZXY Euler
tracks, and version 3 adds explicit incoming and outgoing cubic Hermite
tangents. Euler values stay as radians through interpolation over shortest
angular arcs and become normalized quaternions only at the sampled-pose
boundary. Sampling, blending, events, and playback remain the responsibility
of `dc/animation.h`; the asset layer owns no clock, worker, or alternate
evaluator. The glTF/GLB importer uses the same section for authored STEP,
LINEAR, and CUBICSPLINE TRS clips. Quaternion values are normalized while
their derivative tangents remain unnormalized. Authored morph-weight channels
use the separate `PMW1` instance-binding section described above.

The optional `liblz4.a` addon provides full upstream LZ4 block, high-
compression, Frame, and xxHash APIs plus `pvr_chunk_asset_lz4_decode()`. The
converter's `--emit-asset --lz4-vertices` mode emits one checksummed LZ4 Frame
for the cold vertex partition while leaving the usually smaller polygon/state
partition directly readable. Applications that never link `liblz4` retain the
raw-container path and pay no LZ4 code or runtime-memory cost.

A dictionary is not required. It is most useful for many small related assets,
and only its final 64 KiB can participate in LZ4 matches. Version one records a
dictionary identifier without embedding dictionary bytes; the application
may share one caller-owned dictionary across many models. Keeping that hot
history in ordinary system RAM is the default: expansion-bus SRAM adds bus
ownership and transfer latency but does not make SH-4 dictionary lookups
faster.

For assets laid out on a disc, `pvr_chunk_asset_read_direct()` adds an
explicit, bounded input path. It can issue direct GD-DMA into aligned system
RAM, or stage each command-sized piece in a caller-owned GAPS SRAM lease and
move it with timed G2-DMA. The latter path reports separate GD and G2 timing,
never allocates the 32 KiB bridge window implicitly, and requires the caller
to retain the lease and exclusively own generic G2 channel CH2 or CH3 for the
duration of the call. Both paths validate the exact logical (unpadded)
container after transfer; the sector-rounding tail is not parsed. The
resulting view can feed any of the three decoder policies below.

The decoder supports three policies over the same checked frame: a synchronous
callback, a manually stepped state with a positive output-byte budget, and a
separately linked adapter for the shared KOS fiber-service executor. The
adapter allocates its fixed job ring only when created, borrows an application-
selected fiber stack, and yields between decode steps. It creates neither an
executor nor a thread. Executor shutdown converts active and queued work into
terminal cancellation before returning.

An output budget bounds bytes published during one cooperative step. The Frame
decoder may internally decompress a complete block while satisfying a smaller
destination, so CPU work is additionally capped by the encoded block size. The
converter fixes blocks at 64 KiB and makes them independent. This is a useful
latency ceiling, not a claim that CPU time scales exactly with the output
budget.

## Hierarchies

`pvr_chunk_hierarchy_traverse()` composes caller-owned nodes in array order.
Every parent must precede its child, while `PVR_CHUNK_NODE_NONE` identifies a
root. This topological representation rejects cycles and forward references
without recursion, allocation, or a hidden visited bitmap. NULL model views
are allowed for transform-only grouping nodes.

The caller supplies one `matrix_t`-aligned world matrix per node. The complete
structure is validated before the first workspace write or callback. On
Dreamcast the composition path reaches SH4ZAM through `mat_compose()`; host
tests retain the portable scalar implementation. A callback may continue,
request a successful early stop, or report failure with errno.

Node policy remains explicit in the same caller-owned array. Hidden nodes are
composed so their children retain the correct parent transform, but they do
not invoke the visit callback. A prune-children node is composed and visited;
all descendants are skipped even when the topological array does not store a
subtree contiguously. Translation, rotation, and scale suppression applies to
TRS poses through `pvr_chunk_hierarchy_traverse_poses()` before matrix
construction. Static or already-built matrix inputs must have those component
choices canonicalized by their producer, avoiding an ambiguous matrix
decomposition for reflection, negative scale, or shear.

PCM2 hierarchy sections use fixed little-endian, pointer-free `PCH1` records.
Every node stores one parent index, model ordinal, policy word, and 4x4 local
transform. Section version 1 remains readable and requires its legacy policy
word to be zero; new host output uses version 2 and admits only documented
policy bits.
`pvr_chunk_scene_hierarchy_open()` verifies framing, reserved fields, header
and node checksums, finite matrices, and parent-before-child order before
publishing a view. `pvr_chunk_scene_hierarchy_bind()` then resolves ordinals
through an explicit caller-owned model table into caller-owned runtime nodes.
Transform-only nodes are represented by the stable no-model ordinal. The
binding path allocates nothing, retains no hidden state, and rejects output
overlap with the serialized source.

Canonical PCM2 scene assets can join those pieces without application-side
pointer reconstruction. `pvr_chunk_scene_asset_open()` requires one directly
readable model table and hierarchy, verifies both payload CRCs, validates every
typed model-table ordinal against the enclosing container, and checks every
hierarchy model ordinal before publishing a coherent view.
`pvr_chunk_scene_asset_workspace_query()` combines all persistent model decode
requirements into one aligned nonoverlapping span, while
`pvr_chunk_scene_asset_load()` fills caller-owned contiguous model and node
arrays and binds the final hierarchy atomically. Models that still report a
host-canonicalization requirement are rejected before publication, so a
canonical scene never retains import-only cross-hierarchy execution state.
The helper owns no allocation, decoder, animation state, renderer, list, or
scene lifecycle; applications that need encoded scene metadata can continue
using the individual generic section APIs.

The host scene IR can lower an already resolved cross-hierarchy draw schedule
before serialization. Source topology is retained as transform-only pose
anchors, while ordinary draws become identity children appended in exact
execution order. This keeps transform animation attached to one source node
instead of cloning its tracks. The matching animation serializer appends
identity transform bindings for those proxies and aliases each source node's
visibility channel. A companion morph serializer remaps each source node/model
binding to every matching surviving draw proxy. Capture/replay commands exist
only while the importer resolves the schedule and are absent from PCM2. Static
descendant pruning becomes omission of affected draw proxies, allowing a
pruning node to retain its own scheduled model draw without reintroducing
runtime deferred execution. PMT1 version-two records let those scheduled model
segments share their source vertex stream.

Cross-node deformation is canonicalized through the same complete-pose model.
The host scene IR accepts unordered contributions keyed by model vertex and
producing hierarchy node, validates each node and inverse-bind matrix, merges
duplicate vertex/source pairs, sorts the resulting skeleton by node, remaps
all influences, and uses deterministic largest-remainder quantization so every
vertex sums exactly to 65,535. A rigid reference is represented by one full
weight; staged multi-node accumulation becomes an ordinary general-N skin.
The glTF skin importer uses this canonicalizer as its sole weight emission
path. Both cases serialize into the existing PSG1 and PSK1 sections, so the
target still evaluates a complete pose before rendering and needs no
intermediate vertex-result protocol.

## Rendering boundary

The stream layer describes and validates model data; it does not own PVR scene
or list state. Rendering builds on the existing checked material, geometry,
frustum, texture, and sink interfaces. This keeps asset lifetime separate from
scene lifetime and permits the same validated model to target direct, buffered,
or caller-owned geometry output.

`pvr_chunk_model_emit()` provides the first bounded rendering bridge for
ordinary one-volume strips. It performs a complete support and capacity
preflight, decodes persistent polygon state, assembles canonical vertices in
caller-owned workspace, projects them through the checked SH4ZAM-backed
geometry path, and emits through an existing sink. Compact texture identifiers
are resolved explicitly by a caller callback because the stream does not own
the texture surface, layout, or VRAM address. See `pvr-chunk-rendering.md` for
the state, callback, default-vertex, and failure contracts.

`pvr_chunk_model_emit_prepared()` has the same rendering and failure contract,
but resolves both preflight and emission references through the constant-time
plan. Prepared variants cover ordinary, two-volume, and modifier-volume
emission; they neither change model bytes nor introduce a second render path.

## Draw caches

Frequently drawn ordinary-strip models can move compact decoding entirely out
of the frame loop with `pvr_chunk_model_cache_query()` and
`pvr_chunk_model_cache_build()`. The query reports one deterministic,
32-byte-aligned caller-owned footprint. The build walks an admitted prepared
model once and retains decoded strip state, assembled PVR-native vertices,
canonical position/normal pairs, and original model indices. The completed
cache does not retain or read either compact source stream.

`pvr_chunk_model_cache_emit()` copies one already assembled strip at a time to
the caller's maximum-strip workspace, optionally resolves a skin, morph, or
other deformation result by original vertex index, applies optional per-frame
vertex policy such as lighting, projects it, and publishes through the existing
geometry sink. Material and texture resolution remains an explicit strip
callback, and scene/list ownership remains with the application. The cache
creates no allocator, worker, fiber, texture namespace, or retained renderer.

Two-volume models use the parallel
`pvr_chunk_model_two_volume_cache_query()`,
`pvr_chunk_model_two_volume_cache_build()`, and
`pvr_chunk_model_two_volume_cache_emit()` path. It preserves untextured
two-volume packets at their 32-byte size and textured packets at their 64-byte
size instead of charging every model for a maximum-sized union. Both layouts
retain the same canonical deformation/index sidecar and callback ownership as
the ordinary cache.

Every cached ordinary or two-volume strip also retains the exact object-space
AABB of its admitted reference-pose vertices. The filtered emission variants
invoke caller policy before resolving deformation, preparing a material,
projecting vertices, or publishing output. Static models can classify the
retained box directly with `pvr_chunk_cached_strip_classify()`. A skin, morph,
or other resolver may move vertices beyond the retained reference bound, so
dynamic callers must use a conservative current-pose decision or emit the
strip. `pvr_deform_bounds_calculate()` supplies a whole-pose sphere for model-
level policy; strip-level deformation bounds remain caller policy. This adds
useful sub-model culling without hiding a scene, frustum, pose, or traversal
owner inside the cache.

The model-level `center` and `radius` are consumed by
`pvr_chunk_model_classify()`. It transforms all six homogeneous frustum planes
back into model space and classifies the retained sphere before any stream
walk, vertex lookup, material callback, or projection. `PVR_FRUSTUM_OUTSIDE`
therefore rejects a static model immediately, while `PVR_FRUSTUM_INSIDE`
allows a later renderer to omit per-triangle clipping. Deformed content can
calculate a bound that encloses the complete current pose with
`pvr_deform_bounds_calculate()` before relying on either optimization.

Modifier-volume models use `pvr_chunk_model_modifier_cache_query()`,
`pvr_chunk_model_modifier_cache_build()`, and
`pvr_chunk_model_modifier_cache_emit()`. Admission expands triangles, quads,
and alternating-winding strips through the same topology walker as immediate
emission. The cache retains one 64-byte modifier packet per resulting triangle,
three canonical deformation/index entries, bounded triangle user words, and
the final-triangle boundary for each source volume. Runtime configuration still
selects the modifier list, culling, and include/exclude-last mode; the cache
does not capture scene state. Legacy polygon-cache control records remain
unsupported because these explicit caches replace their hidden global-state
purpose directly.

`pvr_chunk_model_emit_two_volume()` provides the parallel bounded bridge for
inside/outside parameter strips. It keeps primary and secondary texture and
material state distinct, expands two UV sets, projects complete 32-byte or
64-byte PVR vertices, and publishes through a format-bound sink. The complete
stream must use one output layout per call; mismatch, insufficient workspace,
or insufficient memory capacity fails before callbacks or output.

`pvr_chunk_model_emit_modifiers()` expands triangle, quad, and strip volume
records into independent `pvr_modifier_vol_t` packets. It preserves winding,
retains bounded per-triangle user words for an optional callback, projects all
three positions, and terminates every nonempty volume record with an explicit
include or exclude header policy. The application still owns scene and list
lifetime.

Volume records are also available without entering the render path.
`pvr_chunk_volume_iterator_init()` validates the complete record before
publication, and `pvr_chunk_volume_iterator_next()` expands its triangle,
quad, or strip topology one triangle at a time. Quad splitting, alternating
strip winding, reversed strips, and zero to three per-triangle user words use
the same implementation as modifier rendering and prepared modifier caches.
This makes the volume stream directly usable as caller-owned collision or
query geometry without duplicating its framing rules.

The ordinary emitter also decodes compact bump-material records. Their two
signed-normalized basis vectors remain in persistent render state and reach
both strip callbacks. A vertex-policy callback is mandatory while the basis is
active because the model does not own light direction or bump strength. KOS's
existing bump texture and packed offset-color facilities remain the final
application-controlled material step.

## Failure contract

Invalid arguments report `EINVAL`. Truncated, unknown, inconsistent, non-finite,
or out-of-range model contents report `EILSEQ`. Arithmetic that cannot be
represented by the host `size_t` reports `ERANGE`. The optional information
result is initialized to zero before validation and is published only after the
entire model succeeds.
