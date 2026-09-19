# Store-queue safety probe

This regression checks store-queue argument validation, the complete default
eight-level recursive lock capacity, clean rejection of a ninth acquisition,
and successful copies with address translation disabled and enabled. Copies
larger than 1 MiB exercise both 8-byte-aligned and 4-byte-only-aligned sources,
check every copied byte and preserve guard bytes around the destination. The
source includes padding so the historical fast-path +4 MiB advance produces a
safe mismatch rather than an out-of-bounds read in this regression fixture.
The large-copy case temporarily allocates approximately 5 MiB, in this example
only; ordinary applications do not pay for the test buffers.

Success prints `KOSSQ recursion=8 validation=1 mmu=1 batch=1`.

The executable must still be run on an emulator and physical hardware to
validate actual SQ write-back and MMU mapping behavior.
