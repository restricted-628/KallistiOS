# G2 DMA state probe

This target-side probe allocates 32 bytes through the sound-memory allocator,
copies a pattern from SH-4 RAM to AICA RAM and back with blocking G2 DMA, and
checks the terminal channel status after each direction.

The AICA endpoint is intentionally passed as its low physical G2 address. This
exercises the distinction between SH-4 virtual mappings and G2 bus addresses,
including in an MMU-enabled program. The program prints `G2-STATE: PASS` when
the round-trip and both status snapshots succeed.
