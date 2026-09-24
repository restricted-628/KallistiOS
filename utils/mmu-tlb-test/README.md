# MMU invalidation regression

Run `make test` here. Python 3 executes the production SH-4 invalidator in a
small register/MMIO model. No compiler, external package or private SDK is
needed. The test rejects instructions it does not model.

The critical regression is two private translations of the same virtual page
with different ASIDs. An associative write compares against **PTEH.ASID**, not
the low byte of the data being written. Retiring an inactive mapping must clear
its validity without clearing its peer's validity or changing the caller's
PTEH/status. Shared matches, ITLB-only matches and all four hardware page sizes
are covered, along with callee-saved registers and the P2 entry/return sequence.

The target companion is `examples/dreamcast/basic/mmu/tlb-asid`. It also tests
the four page-lifecycle callers, using uncached mappings to isolate TLB work.
It requires the normal KOS multiple-virtual configuration (SV=0). Deliberate
multiple-hit layouts are not legal inputs and are not installed by the probe.

Neither test establishes real-hardware timing, general P0 data translation,
cache coherency, OCINDEX behavior or 32 MiB compatibility. See
`doc/mmu-mapping.md` for the public architecture reference and remaining gates.
