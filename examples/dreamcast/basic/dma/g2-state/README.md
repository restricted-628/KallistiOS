# G2 DMA state probe

This target-side probe allocates 32 bytes through the sound-memory allocator,
copies a pattern from SH-4 RAM to AICA RAM and back with blocking G2 DMA, and
checks the terminal channel status after each direction.

The AICA endpoint is intentionally passed as its low physical G2 address. This
exercises the distinction between SH-4 virtual mappings and G2 bus addresses,
including in an MMU-enabled program. The program prints `G2-STATE: PASS` when
the round-trip and both status snapshots succeed.

It then chains two uploads from an IRQ callback while the first submission's
blocking caller owns its wait result. The expected marker is
`G2-STATE: PASS roundtrip=32 callbacks=2`. This probes the real G2/AICA path,
not the register spies used by the host suite. Failed waits drain any active
transfer before the example returns ownership of its sound allocation.
