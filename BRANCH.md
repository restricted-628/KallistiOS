# G2 DMA and PIO state safety

Branch: `pr/g2-dma-safety`

Role: Stacked upstream candidate

Description snapshot: 2026-09-24

Strengthens existing G2 DMA channel ownership, validation, waiting, cancellation and PIO coexistence.

## Included work

- Exact suspend-state preservation for all four channels, coherent terminal state, and cache/range handling.
- System RAM sizing separate from board-specific PVR sizing.

## Boundaries

- No direct-disc default policy, dynamic BBA driver manager or fiber dependency.

## Dependencies and intended use

Includes ASIC-event ownership. GAPS/direct-disc integration remains a separate extraction.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/g2-dma-safety.md](doc/g2-dma-safety.md)
- [examples/dreamcast/basic/dma/g2-state/README.md](examples/dreamcast/basic/dma/g2-state/README.md)

Reviewed code snapshot: [`8d4de4111138`](https://github.com/restricted-628/KallistiOS/commit/8d4de4111138a2e2dce564aee96b798c272f1387).

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
