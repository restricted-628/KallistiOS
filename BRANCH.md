# Exclusive ASIC event ownership

Branch: `pr/asic-event-ownership`

Role: Narrow upstream candidate

Description snapshot: 2026-09-24

Adds explicit ownership to ASIC events and protects threaded-handler lifetime.

## Included work

- Exclusive claims, legacy API exclusion while claimed, event status and safe threaded-handler cleanup.

## Boundaries

- No direct-disc default policy, fibers, codecs or graphics stack.

## Dependencies and intended use

Independent topic on its recorded upstream base.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/asic-event-ownership.md](doc/asic-event-ownership.md)
- [examples/dreamcast/basic/asic-event-claim/README.md](examples/dreamcast/basic/asic-event-claim/README.md)

Reviewed code snapshot: [`6dc4caafdd29`](https://github.com/restricted-628/KallistiOS/commit/6dc4caafdd297c4c756e03ad2f3ec3dd77e347d5).

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
