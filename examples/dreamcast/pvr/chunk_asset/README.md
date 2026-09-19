# Compact-model asset container

This example converts a small OBJ at build time into one PCM2 compact-model
asset. Its vertex partition is stored as a checksummed LZ4 Frame while the
polygon/state partition remains raw and independently addressable. A portable
resource manifest lists texture identifier 7 and validates it against the
admitted polygon stream before any surface is bound. The host compiler also
stores a pointer-free cooked ordinary-strip cache. The example validates and
materializes that section into the same caller-owned cache type used by the
prepared renderer, while texture identifiers remain dynamically bound at draw
time. A portable hierarchy section carries one identity root referencing model
ordinal zero, with caller-owned general-skin, sparse-morph, and animation
sections exercising the same generic directory loader. The application locates
every section by semantic type rather than by directory ordinal, so the
converter may add sections such as the skeleton and model table without
breaking the example.
`bin2c` embeds the resulting asset only to keep the example self-contained.
`pvr_chunk_asset_section_find_index()` also lets applications query decode
workspace for a section found by type before loading it.

At runtime the application parses the bounded header, queries exact caller-
owned decode workspace, and opts into one LZ4 service fiber on a shared service
executor. Its deliberately tiny test budget publishes at most 16 bytes between
cooperative yields, making the example exercise multiple decode steps. The
normal asset loader then checks both decoded CRCs and passes the streams through
compact-model admission without decoding the vertex partition twice.
The cooked section avoids repeating compact stream traversal and indexed
vertex assembly after loading. Its exact-sized, 32-byte-aligned cache storage
is caller-owned and independently disposable.
The coherent scene-asset loader cross-validates the model table and hierarchy,
loads the complete model set into one persistent decode span, and binds stable
model ordinals directly to caller-owned nodes without a temporary pointer
table. The example also materializes the animation section and samples its
translation through the existing animation runtime rather than through
container-specific code.

After admission, the example stops and destroys its decode executor so no
service thread remains idle during rendering. It binds the sparse shape target
and general skin to the admitted model, samples animation into the hierarchy,
calculates current-pose bounds, and renders the cooked cache through the same
texture-residency, material, and extended-lighting policies used by streamed
models. This is an end-to-end composition example, not retained scene
ownership: every cache, texture, deformation buffer, pose, list, and lifetime
remains explicit application state.

The example renders 120 frames of a lit checker-textured triangle, then holds
the final image for ten seconds without submitting more frames. This allows
visual inspection before cleanup clears VRAM. It then stays on a green PASS
card, or a red FAIL card with the original error code. The serial console logs
the failing stage. `CHUNK_ASSET_HOLD_MS` can be overridden at compile time.

Rebuild the KOS library and this example cleanly after changing public
structure layouts or switching target compilers. An incremental KOS build
can retain objects compiled against old headers; linking alone cannot detect
that mismatch. The example relinks when its KOS or LZ4 archive changes.

The service is an explicit example choice. The same asset can instead use the
synchronous decoder or its manually stepped state, and applications that use
raw assets create no worker thread or fiber and do not need to link `liblz4`.

The generated frame has no dictionary. Shared dictionaries are optional and
become useful mainly when many small, related model partitions are compressed
together under one content pipeline.

Validation on 2026-09-04: the example passed in Flycast interpreter and dynarec
modes, including visual inspection of the textured triangle and the final PASS
card. Asset-loader tests passed GNU17, GCC 14 C23, Clang C2x, and ASan/UBSan;
the converter tests, SH-4 library/example builds, and Doxygen build also passed.
These checks do not replace physical-hardware validation or establish coverage
for arbitrary models beyond this deliberately small composition fixture.
