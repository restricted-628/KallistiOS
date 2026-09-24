# Checked MMU page lifetime

This topic is stacked on `pr/cache-whole-safety`. It adds checked dynamic
mapping, cache/TLB retirement and static-map validation, without changing
default MMU startup, cache mode, the scheduler, or any fiber provider.
On this upstream topic applications explicitly initialize the MMU and own their
mappings. An integrated fork can retain its separate default-startup policy.

## Interfaces and ownership

- `mmu_page_map_ex()` validates a complete range and allocates all missing
  second-level tables before changing any mapping. Invalid arguments and
  allocation failure leave the requested mappings unchanged.
- `mmu_page_unmap_ex()` reports errors, ignores holes, and reclaims empty
  tables. Legacy `void mmu_page_map()` and `void mmu_page_unmap()` remain
  source-compatible wrappers; new code should use the checked forms.
- `mmu_page_set_cache()` requires every requested page to exist before it
  changes any page. `mmu_phys_to_virt()` returns the first matching page.
- Dynamic addresses are 4 KiB page numbers: 19 virtual bits (P0/U0) and
  17 physical bits. ASIDs must be 0-255. Physical pages are not allocated or
  freed by these APIs. Callers own their backing storage and ASID assignment.
- Callers must serialize all table access, mutation, selection and destruction
  across threads. IRQ exclusion during retirement is not a substitute for
  that ownership. Do not reuse backing storage until its mappings are retired.
  Concurrent aliases, DMA ownership and instruction-cache synchronization of
  modified executable pages remain the caller's responsibility.

No heap allocation occurs when all required second-level tables already exist.
On SH-4 each populated table costs 6 KiB and the root context costs 4100 bytes.
Retiring each cached page scans the operand-cache tags; large range retirement
can hold interrupts off for a significant period. This is not a constant-time
or real-time-latency guarantee, and should not be done in an IRQ handler.

## Retirement and initialization

Old cached physical pages are purged before replacement, cache-policy change,
unmap or destruction, including inactive contexts. Matching translations are
retired using the context ASID. The helper temporarily selects PTEH.ASID with
exceptions blocked, performs an associative UTLB invalidation from P2, then
restores PTEH/SR. KOS uses SV=0; shared translations are global matches.
The physical-tag scan is non-associative and skips OCRAM entries.

The implementation follows sections 3.7 and 4.5 of the public
[SH7750/SH7750S/SH7750R hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware).
The cache layout used here is SH7750/SH7750S, not the different SH7750R layout.

Static maps reject invalid enums, either misaligned address, physical truncation
and exhausted reserved-entry capacity. MMU initialization discards stale TLB
entries, resets all four ITLB slots, and installs shared kernel SQ mappings.
SQ data-array updates run from P2. Reinitializing a live MMU still destroys its
translations; it is not a context-switch operation.

TLB misses use the regular complete exception save/restore path instead of the
historical C-call shortcut that did not preserve interrupted PR/MACH/MACL.
Exception handlers must use directly mapped kernel memory, not demand-paged
workspaces. This does not add nested TLB-miss support.

## Validation and limits

`utils/mmu-page-test` compiles the production table-management section with the
real public header. Allocation/IRQ/cache/TLB effects are test substitutes.
It checks allocation rollback, invalid inputs, cross-table mapping, precompiled
PTEs, inactive-context retirement, cache-policy atomicity, reclamation, active
destruction, both legacy signatures and static-map validation/capacity. Static
mapping uses substitute MMIO/TLB-load hooks; it does not compile or model the
exception handlers, copy helpers or hardware instruction timing.

`utils/mmu-tlb-test`, `utils/itlb-reset-test` and `utils/cache-whole-test` cover
production assembly with bounded register/MMIO models. The cache model accepts
`cache.s` as its second argument to exercise the physical-page scan as well.

Target companions are `basic/mmu/tlb-asid`, `basic/mmu/itlb-reset` and
`basic/mmu/mapping-safety`. The mapping probe reports `translation=0` or `1`;
zero is structural coverage only, not evidence of translated data access.
The full ITLB probe remains strict; see `itlb-reset.md` for Flycast's data-array-2
read limitation and its explicitly limited two-array test mode.

Hardware fault-time register preservation, dirty colored-page retirement,
cache/TLB timing and 32 MiB mod behavior remain unverified. This branch makes
no OCINDEX workspace, decoder compatibility or performance claim.
