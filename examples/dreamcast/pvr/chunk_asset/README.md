# Compact-model asset container

This example converts a small OBJ at build time into one PCM2 compact-model
asset. Its vertex partition is stored as a checksummed LZ4 Frame while the
polygon/state partition remains raw and independently addressable. A portable
resource manifest lists texture identifier 7 and validates it against the
admitted polygon stream before any surface is bound. The host compiler also
stores a pointer-free cooked ordinary-strip cache. The example validates and
materializes that section into the same caller-owned cache type used by the
prepared renderer, while texture identifiers remain dynamically bound at draw
time. A portable hierarchy
section carries one identity root referencing model ordinal zero,
with caller-owned general-skin, sparse-morph, and animation sections exercising
the same generic directory loader.
`bin2c` embeds the resulting asset only to keep the example self-contained.

At runtime the application parses the bounded header, queries exact caller-
owned decode workspace, and opts into one LZ4 service fiber on a shared service
executor. Its deliberately tiny test budget publishes at most 16 bytes between
cooperative yields, making the example exercise multiple decode steps. The
normal asset loader then checks both decoded CRCs and passes the streams through
compact-model admission without decoding the vertex partition twice.
The cooked section avoids repeating compact stream traversal and indexed
vertex assembly after loading. Its exact-sized, 32-byte-aligned native cache
storage is caller-owned and independently disposable.
The coherent scene-asset loader cross-validates the model table and hierarchy,
loads the complete model set into one persistent decode span, and binds stable
model ordinals directly to caller-owned nodes without a temporary pointer
table. The example also materializes the animation section and samples its
translation through the existing animation runtime rather than through
container-specific code.

The service is an explicit example choice. The same asset can instead use the
synchronous decoder or its manually stepped state, and applications that use
raw assets create no worker thread or fiber and do not need to link `liblz4`.

The generated frame has no dictionary. Shared dictionaries are optional and
become useful mainly when many small, related model partitions are compressed
together under one content pipeline.
