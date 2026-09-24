# Whole operand-cache maintenance

This topic is stacked on `pr/cache-range-safety`. It adds no MMU startup
policy, page tables, fibers, graphics, or cache-mode changes.

Whole-cache writeback uses a P2 non-associative address-array scan. The former
associative address `0xf4000008` could reinterpret physical tag bits as a P0
virtual address with the MMU enabled. An associative write also cannot serve
as an unconditional indexed purge simply by writing zero. The new scan uses
`0xf4000000 + entry * 32` with association disabled.

A valid dirty line is written back by the hardware before its tag is changed.
Writeback retains the physical tag and valid bit while clearing dirty/reserved
bits. Purge writes zero. Invalid entries are left alone. With OCRAM enabled,
entries 128–255 and 384–511 are neither read nor written. OIX does not change
address-array entry selection, so the scan reaches both cache halves.

Whole-cache purge uses the scan in OIX/ORA modes and size-optimized builds.
The normal speed-oriented path retains its existing 16 KiB eviction buffer.
Existing range-to-whole-cache thresholds remain unchanged. Mode selection is
outside the scan; the loop has no page filter or writeback-vs-purge branch.

The helper saves SR, blocks exceptions during the P2 scan, and restores SR
after the required separation. It does not use the stack, allocate memory,
change CCR/PTEH, or add inline-assembly memory clobbers. CCR mode ownership
remains a caller/system responsibility. This does not validate the separate
cache-mode transition routine or make OIX/OCRAM a safe general allocator.

Public reference: the [Renesas SH7750-family hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware),
sections 4.3.6 and 4.5.3. The layout here is SH7091/SH7750, not the different
SH7750R RAM-mode layout.

## Validation

`make -C utils/cache-whole-test test` executes the production assembly in a
limited register/MMIO model. Its 128 cases cover all OIX/ORA combinations,
valid/invalid and clean/dirty entries, physical tags, undefined read bits,
writeback side effects, OCRAM exclusion, SR restoration and P2 return spacing.
This is not a full CPU, cache pipeline, SDRAM or timing model.

In the integrated fork, pass `kernel/arch/dreamcast/kernel/cache.s` as a second
argument to `asm-scan-test.py` to run 64 additional physical-page cases. That
helper is kept separate from this upstream topic; no MMU lifetime dependency
is required to use the whole-cache helpers.

`examples/dreamcast/basic/cache-whole` checks four public/indexed operations
against a 16 KiB RAM buffer, with MMU off/on and ASID 7, over 16 cases. Build
and run at both normal optimization and `-Os` to cover both public purge
paths. It refuses preselected OIX/ORA modes and never enables them. The marker
`CACHE-WHOLE: PASS cases=16 mmu=2 words=65536 modes=unchanged` is a RAM smoke
result, not proof of real cache tags, hardware writeback, translated mappings,
or stock/modded Dreamcast compatibility. Physical mode-specific tests remain
required; emulator memory can appear coherent without emulating the cache.
