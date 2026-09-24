# Compact UV source codec tests

Run `make test` for the default GNU17 host lane. The shared host-test policy
also supports GCC `HOST_CSTD=c23 HOST_PEDANTIC=-pedantic` and Clang
`HOST_CSTD=c2x HOST_PEDANTIC=-pedantic`. The normal host-test runner discovers
this directory. After sourcing the KOS environment, `make dreamcast` links the
same assertions against KOS for SH-4.

The suite checks:

- An independent 88-byte PUV1 golden with fixed header/payload CRCs.
- Unaligned byte input, every truncation and single-byte corruption,
  repaired-CRC invalid fields, NaN coordinates and trailing data.
- Multiple sources, shared layer bindings, lookup gaps, dense source ranges,
  capacity/alias rejection and untouched outputs on failure.
- PML1 entry/model/count validation against real admitted Compact models.
- Final-reference provenance resolution against literal signed UV8/UV10,
  float-UV and untextured strips. Shared positions retain distinct corner
  tags; reversed strips retain raw order. Late index/tag errors, count and
  framing errors, overflow, aliasing and two-volume input are rejected without
  partially publishing the reference map.
- Decoding and runtime binding into the actual UV renderer, including seams
  on shared position IDs and reversed-strip coordinate order.
- Decoded final references feeding host compiler storage selection with a
  collapsed base mapping, followed by
  independent-coordinate baking, PML1/PUV1 serialization and rendering. Literal
  expected UVs verify that the layer transform is baked once, with identity
  PML1 rows afterward. The SH-4 fixture links the host helper for this test only;
  it is not added to the target KOS library.

No graphics hardware calls are made: the renderer writes a memory sink. Host
stubs fail immediately if a hardware submission is accidentally introduced.
An emulator PASS establishes target-side numerical behavior, not texture-image
or physical-hardware correctness.

The complete byte contract and explicit loading sequence are documented in
`doc/pvr-chunk-uv-sources.md`. Explicit UV-aware scene loading is implemented
separately. Auxiliary material import is not enabled by this test or codec.
