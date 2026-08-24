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

The views deliberately retain unclassified attribute words. Normal, color,
weight, and texture decoding belongs to separately checked render policy, not
to the structural parser.

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

## Failure contract

Invalid arguments report `EINVAL`. Truncated, unknown, inconsistent, non-finite,
or out-of-range model contents report `EILSEQ`. Arithmetic that cannot be
represented by the host `size_t` reports `ERANGE`. The optional information
result is initialized to zero before validation and is published only after the
entire model succeeds.
