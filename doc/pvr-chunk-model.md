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
