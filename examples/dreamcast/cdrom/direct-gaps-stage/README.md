# Direct GD-to-GAPS staging validation

This target-side example validates one deliberately serialized cross-bus
pipeline:

1. lease the complete 32 KiB bridge SRAM window;
2. queue a sixteen-sector direct GD-DMA read into that lease;
3. wait for the request to publish terminal state and release G1;
4. copy the SRAM payload to system RAM with blocking G2 DMA; and
5. compare it byte-for-byte with the same sectors read through the normal
   disc interface.

The example prints `DIRECT-GAPS-STAGE: SKIP` when no compatible bridge is
present or its SRAM is already owned by an initialized network driver. It
never overlaps G1 and G2 DMA. A passing emulator run requires bridge, G2 DMA,
and direct optical-drive emulation; physical hardware validation remains
required.
