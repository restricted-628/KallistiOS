# Compact-model rendering

`pvr_chunk_model_emit()` and `pvr_chunk_model_emit_two_volume()` are bounded
bridges between admitted compact model streams and KOS PVR geometry sinks.
They are deliberately emitters, not a retained renderer: the application still
owns the scene, active list, materials, textures, model storage, animation
state, and workspace.

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

These records are not skipped or approximated. Two-volume texture, material,
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

## Concurrency and lifetime

The model view and both source streams must remain immutable for the complete
call. Workspace may not overlap either stream, the transform matrix, or a
memory sink; a memory sink must also remain separate from the streams and
matrix. Callbacks execute synchronously on the caller's thread. They may use
application asset tables, but must not mutate the active model, workspace, or
sink. The emitter retains no pointer after return.
