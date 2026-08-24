# Compact-model rendering

`pvr_chunk_model_emit()` is the bounded bridge between admitted compact model
streams and KOS's canonical PVR geometry sinks. It is deliberately an emitter,
not a retained renderer: the application still owns the scene, active list,
materials, textures, model storage, animation state, and workspace.

## Pipeline

One call performs these stages:

1. Preflight the complete polygon stream, matrix, workspace, sink, supported
   record families, state ranges, every referenced vertex, and total memory
   sink capacity.
2. Decode state records into `pvr_chunk_render_state_t`.
3. Resolve each indexed strip reference through the admitted model view.
4. Assemble one canonical `pvr_vertex_t` strip in caller-owned aligned
   workspace, correcting reversed-strip winding by exchanging the first two
   references.
5. Apply the explicit object-to-screen matrix through
   `pvr_geometry_project()`. On Dreamcast this batch uses SH4ZAM while
   preserving the caller's XMTRX state.
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

- resolve `state->texture.identifier` in an asset table;
- build or select a `pvr_poly_cxt_t` for that texture and list;
- apply the decoded blend, filter, clamp, flip, mipmap, color, and strip policy;
- compile or select a checked `pvr_material_t`; and
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

## Preflight and failure

Built-in validation finishes before the first callback or sink write. A memory
sink also reserves capacity for the complete model up front, so `ENOSPC` cannot
leave a partial model there. Reserved state bits and invalid exponents report
`EILSEQ`; arithmetic overflow reports `ERANGE`.

Callbacks, projection, and non-memory sink publication can still fail after a
prior strip was emitted. `pvr_chunk_render_result_t` and the sink's emitted
counter then describe the complete valid prefix. A callback returning a
negative value should set errno; the emitter uses `EIO` if it did not.

The first renderer path intentionally fails with `ENOTSUP` during preflight for
record families that need a different TA contract:

- modifier-volume geometry;
- two-volume texture, material, and strip records;
- bump materials; and
- cached-polygon control records.

These records are not skipped or approximated. Later dedicated paths can add
their required vertex formats and header state without weakening the ordinary
strip contract.

## Concurrency and lifetime

The model view and both source streams must remain immutable for the complete
call. Workspace may not overlap either stream, the transform matrix, or a
memory sink; a memory sink must also remain separate from the streams and
matrix. Callbacks execute synchronously on the caller's thread. They may use
application asset tables, but must not mutate the active model, workspace, or
sink. The emitter retains no pointer after return.
