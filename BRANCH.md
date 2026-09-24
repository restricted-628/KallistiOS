# Ordered cache-mode transitions

Branch: `pr/cache-mode-transition`

Role: Stacked upstream candidate

Description snapshot: 2026-09-24

Protects CCR read/modify/write and retires cache state according to the old mode before applying the new one.

## Included work

- P2 execution, exception exclusion, old-layout retirement and tag invalidation.
- Documented scratchpad addressing and caller quiescence responsibilities.

## Boundaries

- No cache-mode default change, workspace allocator or demonstrated 32 MiB compatibility fix.

## Dependencies and intended use

Includes whole-cache and range safety; keep their implementation prerequisites in review order.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/cache-transition.md](doc/cache-transition.md)
- [doc/cache-whole.md](doc/cache-whole.md)

Reviewed code snapshot: [`3194d1ee13ba`](https://github.com/restricted-628/KallistiOS/commit/3194d1ee13ba4bf9c711430e567a644d865c5473).

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
