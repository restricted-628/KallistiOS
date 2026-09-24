# Complete core-fiber SDK bundle

Branch: `pr/core-fibers`

Role: Complete SDK bundle

Description snapshot: 2026-09-24

One integrated checkout with the KOS core fiber provider and the surrounding SDK prerequisites. Despite its pr/ prefix, this is not a narrow upstream submission.

## Included work

- Core fiber continuation, synchronization and optional XMTRX preservation.
- Integrated direct-disc/G1/G2/GAPS support, fiber-disc and deferred VBlank adapters, RAM/VRAM examples.

## Boundaries

- No Fiber Service Executor or alternative SH4ZAM fiber runtime. Unrelated graphics still depend on SH4ZAM.

## Dependencies and intended use

Mutually exclusive with addon/fiber-service-sh4zam. Do not combine provider archives. For upstream review use pr/core-fibers-submission.

This branch includes integrated fork history. Do not submit its complete diff
as a single upstream kernel change.

## Source and detailed contracts

- [FIBER-BUNDLE.md](FIBER-BUNDLE.md)
- [doc/fiber-bundle-validation.md](doc/fiber-bundle-validation.md)
- [doc/fiber-vblank.md](doc/fiber-vblank.md)

Reviewed code snapshot: [`0a930044af9d`](https://github.com/restricted-628/KallistiOS/commit/0a930044af9deaeade2126dac67db8573dfa5006).

Common ancestor with the inspected upstream master:
[`55da82831f4c`](https://github.com/restricted-628/KallistiOS/commit/55da82831f4cadef233b02c95493011428fb917f).
This records the inspected baseline, not a claim of being rebased to today's upstream.

The description pass changes documentation only. It does not rerun code tests,
prove hardware behavior, or certify every inherited feature. Follow the linked
contracts and reproduce the relevant host, target and emulator checks; physical
hardware validation remains a separate gate. Private research and uncommitted
experiments are not part of this description.

[All published branches](https://github.com/restricted-628/KallistiOS/blob/master/BRANCHES.md) ·
[Integrated fork differences](https://github.com/restricted-628/KallistiOS/blob/master/FORK.md) ·
[Official KallistiOS](https://github.com/KallistiOS/KallistiOS)
