# VFS, name manager and module lifetimes

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Storage + kernel**.

## Scope and entry points

Descriptor/open-file references, handler publication/removal, mount teardown and export/library lifetime.

- `kernel/fs/fs.c`
- `kernel/exports/nmmgr.c`
- `kernel/exports/exports.c`
- `kernel/exports/library.c`
- `include/kos/fs.h`
- `include/kos/nmmgr.h`
- `include/kos/library.h`
- `addons/libkosfat`
- `addons/libkosext2fs`

Relevant existing documentation (dated claims must be rechecked):

- `doc/fs-object-lifetime.md`

## Preserve these boundaries

- Retain handlers and in-flight operation state through callbacks; unpublish before drain. Define ownership on failed handle construction explicitly.
- Do not call filesystem callbacks while holding the descriptor-table lock or drain while holding a lock needed by final close.
- Borrowed raw handles, mmap pointers and export entries do not become indefinitely owned. Socket/poll users need separate reconciliation.
- Keep hardware, IRQs and scheduling available for final closes; lifecycle calls remain externally serialized.

## Coordinate before changing

- Network socket adapter
- Disc/ISO9660
- VMU storage
- Kernel global shutdown

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/nmmgr-lifecycle-test` — candidate `make -C utils/nmmgr-lifecycle-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/basic/fs-lifetime`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Lifetime protection does not make concurrent offsets atomic or solve all socket-close races.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
