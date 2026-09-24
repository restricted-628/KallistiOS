# MMU mapping and TLB safety

This fork enables the Dreamcast MMU during default startup (`INIT_MMU` is part
of `INIT_DEFAULT_ARCH`), after exception handling is installed and before
peripheral initialization. Shutdown disables it after workers and hardware
users stop. No page tables, service thread, or decoder are implicitly created.
Kernel P1/P2 addresses keep their direct mapping; P0 pointers require mappings.

An application that needs the former startup behavior can use
`KOS_INIT_FLAGS(INIT_DEFAULT & ~INIT_MMU)`. Custom flags without `INIT_MMU`
also leave it off. Code calling `mmu_init()` itself should first check
`mmu_enabled()`; reinitializing a live MMU discards its mappings.

This default is a fork policy, not an upstream KOS compatibility claim.
Bootloader/DC-load, external libraries, graphics and physical-hardware coverage
still need expanding beyond the probes below.

## Checked page lifecycle

`mmu_page_map_ex()` is the error-reporting counterpart to the legacy
`mmu_page_map()` interface. It validates the complete range and allocates every
required second-level page table before publishing any mapping. Allocation
failure therefore leaves the requested range unchanged.

Virtual and physical addresses passed to the dynamic mapping API are 4 KiB page
numbers. The virtual range covers the 2 GiB P0/U0 region; physical pages are
limited to the 512 MiB physical address space.

The related checked operations are:

- `mmu_page_unmap()`, which ignores already-unmapped pages and reclaims empty
  second-level tables;
- `mmu_page_set_cache()`, which changes an entirely mapped range or changes
  nothing when any page is absent;
- `mmu_phys_to_virt()`, which returns the first virtual page mapped to a given
  physical page.

The old `mmu_page_map()` symbol and signature remain available. It delegates to
the checked mapper but cannot report an error, so new code should prefer
`mmu_page_map_ex()`.

Page-table mutation is not internally serialized. An application using the MMU
from multiple threads must serialize mapping, unmapping, cache-policy changes,
context switching, and context destruction.

## Cache and TLB retirement

Mapping replacement, unmapping, cache-policy changes, and context destruction
retire matching UTLB entries through an associative write. A matching ITLB
entry is retired by the same hardware operation. The array write executes from
P2 and observes the required instruction separation before returning to cached
or translated code.

Cached data is retired by scanning physical tags from P2 before a cached mapping
is changed. A P1 `ocbp` operand is insufficient: a translated page can have a
different virtual cache index, especially when OIX uses virtual bit 25. The
scan visits 512 entries per cached page, skips OCRAM entries, and writes back
and invalidates matching valid lines even for an inactive context. There is no
temporary eviction buffer. This is lifecycle work, not a per-frame operation.
Context destruction also clears the current page-table pointer before release.

Whole-cache purge uses a P2 tag scan in OIX/ORA modes (and size-optimized builds).
A canonical eviction buffer cannot evict the other OIX half. The normal-mode
speed-oriented implementation is unchanged.
Whole-cache writeback also uses the non-associative P2 scan, preserving valid
tags while clearing dirty bits. Associative array writes must not reinterpret
physical tags as P0 virtual addresses under the MMU.

TLB misses now use the complete exception save/restore path. The old shortcut
could overwrite interrupted PR/MACH/MACL while calling C; it was not a valid
asynchronous register-preservation contract. IRQ handlers must not touch
demand-paged workspaces: this path does not provide nested TLB-miss handling.

Copy-back and write-through policies now set the page-table cache bits instead
of both being encoded as uncached. The caller's requested initial dirty state is
also preserved.

## Static mappings

`mmu_page_map_static()` validates the page-size and protection enums, alignment
of both addresses, the complete 29-bit physical span, and the available safe
TLB capacity. Invalid physical addresses can no longer be silently truncated
into a different mapping.

MMU initialization invalidates inherited TLB contents before reserving the two
store-queue translations. Their shared bit stays set across destination updates,
so kernel SQ operations remain usable with a nonzero current ASID. SQMD still
restricts access to privileged code. Existing SQ locking remains required.

## Translated decoder workspaces

OIX selects a cache half with virtual address bit 25. A translated P0 workspace
can have that bit set while mapping to canonical physical RAM with it clear,
in either the lower or upper 16 MiB. P1/P2 pointers cannot be remapped this way.
This is a candidate mapping strategy, not evidence of physical-mod compatibility.

Use one cached view for the lifetime of a workspace. Purge an allocation's P1
view before transferring ownership to the translated view; do not keep using
both. Stop/join every consumer before unmapping or freeing it. DMA needs an
explicit physical-address and cache-maintenance handoff, not a masked virtual
pointer. The ADX probe keeps compressed/PCM/DMA staging buffers disjoint in P1.

MMU context and CCR are system-wide state, not fiber-local state. The example
owns a single context across main and the service executor; it does not flip
OIX or ASIDs on yields. Production integration must negotiate context, virtual
range and cache-mode ownership with the application. No library auto-enables
OIX, and the existing OCRAM layout must not be used concurrently with it.

## Validation

`examples/dreamcast/basic/mmu/mapping-safety` checks argument validation,
page-table translations, cache-policy encoding and atomicity, remapping,
unmapping, targeted TLB retirement, and active-context teardown.

The example can complete its structural checks when a runtime does not apply
general P0 translation. A physical-hardware run is still required to confirm
translated data access, stale live-translation replacement, cache-policy
behavior, and the P2 instruction-separation contract.

### MMU-on decoder tranche (2026-09-22)

- Forced full SH-4 GCC 16.2 KOS rebuild and target builds of ADX,
  fiber-MMU and OIX-workspace probes passed. Assembly inspection confirmed the
  P2 non-associative scan and full-context TLB-miss dispatch. This is not a
  hardware execution test of the scan.
- ADX codec/pipeline host tests passed GCC 14 GNU17/C23; the pipeline also
  passed Clang C2x with ASan/UBSan and strict warnings.
- Installed Flycast: MMU-on direct-P1 ADX controls passed on 16 MiB/interpreter
  and 32 MiB/dynarec, both with zero producer starvation and 176,402 source
  bytes. Host sound was muted; no audible playback claim.
- Fiber/SQ probe passed with ASID 7 on 16 MiB/interpreter. The explicit
  `INIT_DEFAULT & ~INIT_MMU` startup control also passed.
- General translated ADX state (including the OIX variant) was refused by the
  runtime's translation check. The 32 MiB cache-workspace probe likewise
  reported UNAVAILABLE with zero completed cases. Do not count either as PASS.

Next validation requires general-MMU/cache-capable execution, then stock and
modded physical consoles. Nonzero-ASID SQ behavior, fault-time register
preservation, dirty colored-page retirement and upper-bank results remain
hardware gates. The native Sofdec demux/video core is still separate unfinished
work; no retail decoder compatibility fix is claimed.
