# Bundled LZ4

KallistiOS builds the upstream LZ4 1.10.0 library as the optional static
archive `addons/lib/dreamcast/liblz4.a`. The public LZ4 block, high-compression,
Frame, and xxHash headers are available from `addons/include`.

`<kos/pvr_chunk_asset_lz4.h>` supplies the optional LZ4 Frame decoder callback
for versioned compact-model assets. It accepts one exact frame, checks its
declared content size and dictionary identifier, leaves frame checksums on,
and rejects trailing bytes. Dictionaries remain caller-owned and optional.

The vendored library files are byte-identical to the upstream `v1.10.0`
release (`ebb370ca83af193212df4dcbadcc5d87bc0de2f0`) from
<https://github.com/lz4/lz4> and remain under their BSD 2-Clause license.
KOS-specific build policy is kept in the surrounding Makefile instead of
patching those sources.

The archive is not linked into applications that do not request it. Frame
compression uses heap-backed context storage on Dreamcast so a one-shot
encoder does not place its large state on a normal thread or fiber stack.
