# Direct GD-to-GAPS staging validation

This target-side example validates one deliberately serialized cross-bus
pipeline:

1. acquire exclusive STAGING ownership and lease the complete 32 KiB window;
2. queue a sixteen-sector direct GD-DMA read into that lease;
3. wait for the request to publish terminal state and release G1;
4. copy the SRAM payload to system RAM with blocking G2 DMA; and
5. compare it byte-for-byte with a direct-driver read of the same sectors.

The example prints `DIRECT-GAPS-STAGE: SKIP` when no compatible bridge is
present, another owner holds it, or a resident IP/unclassified dcload loader
protects it. Boot via disc or known serial dcload for staging; merely stopping
KOS networking does not detach the resident IP loader. This example never
overlaps G1 and G2 DMA. NETWORK owners can still authorize G1 through their
own leases; this standalone staging example cannot borrow those leases.
A passing emulator run requires bridge, G2 DMA,
and direct optical-drive emulation; physical hardware validation remains
required.
