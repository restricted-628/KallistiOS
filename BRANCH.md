# Whole operand-cache maintenance

Branch: `pr/cache-whole-safety`

Role: Stacked upstream candidate

Description snapshot: 2026-09-24

Uses non-associative indexed maintenance for whole-cache writeback/purge and respects active OCRAM storage.

## Included work

- Physical-tag writeback/purge, exception exclusion and scratchpad-entry exclusion.

## Boundaries

- No startup-mode changes, MMU tables, fibers or graphics. Not a general OCINDEX/OCRAM workspace allocator.

## Dependencies and intended use

Includes pr/cache-range-safety. Prerequisite for cache-mode transition and MMU page retirement.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/cache-whole.md](doc/cache-whole.md)
- [doc/cache-maintenance.md](doc/cache-maintenance.md)

Reviewed code snapshot: [`e7ece6595276`](https://github.com/restricted-628/KallistiOS/commit/e7ece65952762a6ce9163ed7f80ef66ed2f407ce).

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
