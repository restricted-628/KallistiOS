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
normalizes eight-bit or ten-bit UV coordinates, expands signed normals and
ARGB color, and retains bounded per-triangle user words.

`pvr_chunk_model_vertex_attributes_get()` resolves an index directly from an
admitted model without allocating memory. Admission rejects overlapping
vertex ranges, so the answer is deterministic. The lookup scans bounded
records; a renderer that needs constant-time resolution can construct an
index in caller-owned workspace.

Both decoded forms retain access to the raw bounded views, allowing uncommon
application policy without weakening the checked framing boundary.

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
