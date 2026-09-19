# Compact draw-cache regression

`make test` exercises the actual model, renderer, cache, cooked-cache,
toon/wire and clipping code against host geometry sinks. The shared host-test
runner discovers this suite for GNU17 and strict C23 lanes. After sourcing the
KOS environment, `make dreamcast` builds the same suite for SH-4.

The independent-UV cases check two strips sharing position IDs but carrying
different per-corner UVs, reversed reference order, callback-visible UVs,
filtered-strip identity, direct/prepared packet equality, and cache independence
after the borrowed coordinates' lifetime ends. A near-plane-crossing triangle
checks independently calculated intersection UVs; DROP emits nothing. Admission
tests check coordinate counts, short index capacity, nonfinite values, aliases
and unchanged outputs on failure.

Memory-sink packets and emulator assertions do not prove physical texture
filtering, translucent ordering or pixel/image conformance. Those remain
separate graphics validation gates.
