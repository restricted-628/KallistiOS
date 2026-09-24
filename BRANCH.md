# Bounded cache-range maintenance

Branch: `pr/cache-range-safety`

Role: Narrow upstream candidate

Description snapshot: 2026-09-24

Corrects alias normalization and range admission for existing cache APIs.

## Included work

- P2-to-P1 normalization, preserved translated P0/P3 addresses, no-op empty ranges and rejection of wrap/cross-area spans.
- One normalization/admission step before bounded loops, with code-generation and regression checks.

## Boundaries

- No cache-mode or MMU startup policy, allocations, fibers or renderer.

## Dependencies and intended use

Base mechanism for whole-cache and instruction-index topics.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/cache-maintenance.md](doc/cache-maintenance.md)
- [examples/dreamcast/basic/cache-safety/README.md](examples/dreamcast/basic/cache-safety/README.md)

Reviewed code snapshot: [`515ca27c68d7`](https://github.com/restricted-628/KallistiOS/commit/515ca27c68d7d10c81a8e7804f25f277a61fdeda).

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
