# Checked MMU mapping and page retirement

Branch: `pr/mmu-page-lifetime`

Role: Stacked upstream candidate

Description snapshot: 2026-09-24

Adds checked mapping/unmapping and cache/TLB retirement before backing pages are reused.

## Included work

- Allocation rollback, cache-policy changes, table reclamation, static-map validation and ASID-aware invalidation.
- ITLB reset correction and full exception-save path for TLB misses.

## Boundaries

- No MMU-on default, automatic physical-page allocator, fiber-private address spaces or blanket 32 MiB RAM-mod compatibility guarantee.

## Dependencies and intended use

Includes whole-cache/range prerequisites and overlaps the separate ITLB reset topic. Reconcile duplicate fixes before upstream submission.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/mmu-page-lifetime.md](doc/mmu-page-lifetime.md)
- [doc/itlb-reset.md](doc/itlb-reset.md)

Reviewed code snapshot: [`13c345bc98b8`](https://github.com/restricted-628/KallistiOS/commit/13c345bc98b80439cff29ece7b5c4e2effe61a3c).

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
