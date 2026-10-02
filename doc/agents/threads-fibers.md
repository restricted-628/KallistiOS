# Threads, core fibers and exclusive providers

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Fibers/services**.

## Scope and entry points

Carrier-thread context, cooperative scheduling, fiber synchronization and the complete alternative providers.

- `include/kos/thread.h`
- `include/kos/fiber.h`
- `include/kos/fiber_sync.h`
- `kernel/thread/thread.c`
- `kernel/thread/fiber.c`
- `kernel/thread/fiber_sync.c`
- `kernel/arch/dreamcast/kernel/fiber_context.c`
- `kernel/arch/dreamcast/kernel/fiber_switch.s`

Relevant existing documentation (dated claims must be rechecked):

- `FORK.md`
- `BRANCHES.md`

## Preserve these boundaries

Branch-specific entry points (not all exist on master):

- `addon/fiber-service-sh4zam`: `addons/libfiber_sh4zam/src`, `tests`,
  `check-provider.sh` and its provider-specific service header.
- Complete bundles: `FIBER-BUNDLE.md`, `doc/fiber-bundle-validation.md`,
  `utils/check-fiber-bundle.sh` and `utils/fiber-bundle-provider`.
- Core bundle/submission: inspect `doc/core-fibers.md` where present.
  Verify the actual branch tip before using any of these; do not copy files
  between bundles to make an audit pass.

- Keep the core/upstream provider independent of SH4ZAM and the executor; the full addon provider is an exclusive alternative, not a fallback layer.
- Check thread ownership even on already-ready/fast-return event paths. Compare current master with both bundle implementations before merging runtime fixes.
- Preserve carrier TLS/errno/address-space semantics, FPSCR/register state and logical-stack handling; do not infer process isolation.
- Inspect both bundle link-audit scripts and symbols in their actual checkouts; whole-archive dual-provider linkage must fail. Merely finding a script is not a pass.

## Coordinate before changing

- Services for executor semantics
- Kernel for scheduler/IRQ/MMU/SQ
- Storage/networking for optional waits

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/irq-stack-test` — candidate `make -C utils/irq-stack-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/basic/threading/fiber`
- `examples/dreamcast/basic/threading/fiber-sync`
- `examples/dreamcast/basic/threading/fiber-context-probe`
- `examples/dreamcast/basic/threading/fiber-mmu-probe`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Current bootstrap reports an event-wait ownership discrepancy; reproduce and review it in a separately assigned task before fixing.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
