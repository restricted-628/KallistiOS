# Instruction-cache range maintenance with IIX

`arch_icache_inval_range()` and `arch_icache_sync_range()` select the active
instruction-cache indexing scheme once per call, with exceptions blocked.
The normal-mode loop is unchanged. A separate IIX loop substitutes effective
address bit 25 for bit 12 when selecting the cache-address-array entry.

For the Dreamcast's SH-4-compatible 256-entry instruction cache:

| CCR.IIX | Entry-array byte offset |
| --- | --- |
| 0 | `address & 0x1fe0` |
| 1 | `(address & 0x0fe0) \| ((address >> 13) & 0x1000)` |

Instruction fetch indexing and direct cache-array addressing are distinct:
array addresses always select entries with bits 12:5. See sections 4.4.3 and
4.5.1 of the public [SH7750/SH7750S/SH7750R hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware).
This implementation uses the SH7750/SH7750S layout, not SH7750R double-size mode.

The IIX loop uses non-associative invalidation from P2, so invalidating an
instruction entry does not depend on finding a translation in the ITLB.
The sync variant issues OCBWB through the original effective data address
(after the existing P2-to-P1 normalization) before invalidating each entry.
It does not substitute an instruction-array address for the data operand.

This does not enable IIX, alter OIX/ORA, change MMU startup, or choose a RAM
workspace policy. Callers still need to use the effective execution address
to invalidate the intended instruction-cache color, and separately synchronize
other aliases where required. Sync needs valid, resident mappings for its
data-cache operations; this is not an inactive-context physical-page API.
Existing range validation, IRQ/SR restoration and P2 return spacing remain.
No heap allocation, FPU use, per-entry mode check or new memory clobber is added.

## Tests

- `make -C utils/icache-iix-test test` executes both production routines in a
  register/MMIO model: 16,848 cases spanning normal/IIX modes, both halves,
  bit-12 disagreement, 4 KiB/8 KiB/32 MiB boundaries, P2 normalization, invalid
  ranges, writeback order, mode-read count, P2 execution and saved state.
- Existing `utils/cache-range-test` covers range arithmetic and preflight.
- `examples/dreamcast/basic/icache-iix` seeds synthetic IC tags, invokes the
  production routines from P2, checks all 256 validity bits, clears the test
  tags and restores CCR. All 36 cases pass in Flycast interpreter and dynarec.
  Only invalidate tests use the A25 RAM mirror; sync uses ordinary backed RAM.

The target probe deliberately invalidates the whole instruction cache during
setup/cleanup. It never executes the synthetic entries. Model/array results
are not proof of hardware instruction-fetch coherence, timing, general MMU
translation, 32 MiB mod compatibility, or a performance gain. Those gates stay
open until measured on appropriate hardware.
