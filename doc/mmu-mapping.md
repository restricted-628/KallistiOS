# MMU mapping and TLB safety

The Dreamcast MMU remains optional and disabled during normal KOS startup.
These changes add no thread, periodic work, permanent buffer, or runtime cost
to applications that do not initialize it.

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

Cached data is purged through each mapping's P1 physical alias before a cached
mapping is changed, including mappings in an inactive context that may retain
dirty lines from its last use. This avoids requiring the whole-cache eviction
workspace. Context destruction also clears the current page-table pointer
before releasing the tables.

Copy-back and write-through policies now set the page-table cache bits instead
of both being encoded as uncached. The caller's requested initial dirty state is
also preserved.

## Static mappings

`mmu_page_map_static()` validates the page-size and protection enums, alignment
of both addresses, the complete 29-bit physical span, and the available safe
TLB capacity. Invalid physical addresses can no longer be silently truncated
into a different mapping.

MMU initialization invalidates inherited TLB contents before reserving the two
store-queue translations. Store-queue ownership and mapping policy are outside
the scope of this topic.

## Validation

`examples/dreamcast/basic/mmu/mapping-safety` checks argument validation,
page-table translations, cache-policy encoding and atomicity, remapping,
unmapping, targeted TLB retirement, and active-context teardown.

The example can complete its structural checks when a runtime does not apply
general P0 translation. A physical-hardware run is still required to confirm
translated data access, stale live-translation replacement, cache-policy
behavior, and the P2 instruction-separation contract.
