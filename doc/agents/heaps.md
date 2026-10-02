# Caller-backed heaps and allocation policy

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Kernel/platform**.

## Scope and entry points

Independent arenas, allocation lifetimes, validation and memory budgets.

- `kernel/mm/heap.c`
- `include/kos/heap.h`
- `kernel/mm/Makefile`

Relevant existing documentation (dated claims must be rechecked):

- `doc/independent-heaps.md`

## Preserve these boundaries

- A caller-backed arena is not memory protection. The caller retains backing storage and must quiesce all users before destroy.
- Preserve 32-byte alignment, checked arithmetic, split/coalesce and corruption admission rules.
- Do not claim constant-time or IRQ-safe behavior: whole-heap validation and copying can occur under a mutex.
- Keep NetBSD bounded pools and subsystem budgets separate from a global allocator replacement.

## Coordinate before changing

- Network pools
- Graphics/audio budgets
- VFS and library teardown

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/mm-heap-test` — candidate `make -C utils/mm-heap-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/basic/independent-heap`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

A stats or validation API does not make stale handles or arbitrary backing-memory corruption safe.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
