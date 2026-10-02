# Cache, MMU, OCRAM and store queues

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Kernel/platform**.

## Scope and entry points

SH-4 cache/TLB maintenance, address aliases, page lifetime, OCRAM and SQ ownership.

- `kernel/arch/dreamcast/kernel/cache.s`
- `kernel/arch/dreamcast/kernel/cache_all.s`
- `kernel/arch/dreamcast/kernel/mmu.c`
- `kernel/arch/dreamcast/kernel/itlb.s`
- `kernel/arch/dreamcast/hardware/sq.c`
- `kernel/arch/dreamcast/include/arch/cache_range.h`
- `kernel/arch/dreamcast/include/arch/mmu.h`
- `kernel/arch/dreamcast/include/dc/sq.h`

Relevant existing documentation (dated claims must be rechecked):

- `doc/cache-maintenance.md`
- `doc/cache-transition.md`
- `doc/cache-whole.md`
- `doc/icache-iix.md`
- `doc/itlb-reset.md`
- `doc/mmu-page-lifetime.md`
- `doc/store-queue-safety.md`

## Preserve these boundaries

- Keep virtual/physical aliases, translated/untranslated regions and caller ownership distinct. MMU-on does not make every address translated.
- Before changing cache mode, reason from the old layout and retire state without corrupting active OCRAM. Do not treat OCINDEX as locked/general-purpose RAM.
- Mapping removal must cover backing-page cache state and relevant TLB/ASID lifetime; preserve legacy ABI wrappers where required.
- SQ ownership is recursive/serialized state, not just a copy loop. Audit area boundaries, mapping restoration, context switches and error cleanup.

## Coordinate before changing

- IRQ/DMA and direct disc
- Graphics workspace and texture upload
- Audio stereo SQ
- Fiber context

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/cache-range-test` — candidate `make -C utils/cache-range-test test` in an assigned checkout.
- `utils/cache-transition-test` — candidate `make -C utils/cache-transition-test test` in an assigned checkout.
- `utils/cache-whole-test` — candidate `make -C utils/cache-whole-test test` in an assigned checkout.
- `utils/icache-iix-test` — candidate `make -C utils/icache-iix-test test` in an assigned checkout.
- `utils/itlb-reset-test` — candidate `make -C utils/itlb-reset-test test` in an assigned checkout.
- `utils/mmu-page-test` — candidate `make -C utils/mmu-page-test test` in an assigned checkout.
- `utils/mmu-tlb-test` — candidate `make -C utils/mmu-tlb-test test` in an assigned checkout.
- `utils/sq-test` — candidate `make -C utils/sq-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/basic/cache-safety`
- `examples/dreamcast/basic/mmu`
- `examples/dreamcast/basic/sq-safety`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Host models and emulator results do not prove cache/DMA visibility or RAM-mod behavior.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
