# VBlank priority and callback lifetime

Branch: `pr/vblank-priority-safety`

Role: Narrow upstream candidate

Description snapshot: 2026-09-24

Adds explicit-priority callback registration and safe removal while retaining legacy registration order.

## Included work

- Lower priorities first; newest first for equal explicit priorities. Legacy API remains FIFO.

## Boundaries

- No fibers, worker thread or Service Executor dependency; deferred fiber work belongs to an optional adapter.

## Dependencies and intended use

Independent IRQ mechanism used by the integrated fiber-VBlank adapters.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/vblank-callback-safety.md](doc/vblank-callback-safety.md)
- [examples/dreamcast/basic/threading/vblank-priority/README.md](examples/dreamcast/basic/threading/vblank-priority/README.md)

Reviewed code snapshot: [`c05800f3b649`](https://github.com/restricted-628/KallistiOS/commit/c05800f3b6496a2429fff12ca39ee0290f393b54).

Common ancestor with the inspected upstream master:
[`804b3195ebd1`](https://github.com/restricted-628/KallistiOS/commit/804b3195ebd1a06a27cc2b3a5eacf7a2429040a3).
This records the inspected baseline, not a claim of being rebased to today's upstream.

The description pass changes documentation only. It does not rerun code tests,
prove hardware behavior, or certify every inherited feature. Follow the linked
contracts and reproduce the relevant host, target and emulator checks; physical
hardware validation remains a separate gate. Private research and uncommitted
experiments are not part of this description.

[All published branches](https://github.com/restricted-628/KallistiOS/blob/master/BRANCHES.md) ·
[Integrated fork differences](https://github.com/restricted-628/KallistiOS/blob/master/FORK.md) ·
[Official KallistiOS](https://github.com/KallistiOS/KallistiOS)
