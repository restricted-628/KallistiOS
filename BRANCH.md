# Caller-backed independent heaps

Branch: `pr/independent-heaps`

Role: Narrow upstream candidate

Description snapshot: 2026-09-24

Adds opt-in allocator arenas over caller-provided storage.

## Included work

- Aligned first-fit allocation, realloc/free, inspection, corruption rejection and isolated arena tests.

## Boundaries

- No replacement of the global allocator, memory protection, IRQ-safe allocation or constant-time guarantee.

## Dependencies and intended use

Independent topic; no graphics or fiber policy prerequisite.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/independent-heaps.md](doc/independent-heaps.md)
- [examples/dreamcast/basic/independent-heap/README.md](examples/dreamcast/basic/independent-heap/README.md)

Reviewed code snapshot: [`e281f26ca415`](https://github.com/restricted-628/KallistiOS/commit/e281f26ca415269d976029370e093768f9758563).

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
