# Store-queue ownership and mapping

Branch: `pr/store-queue-safety`

Role: Narrow upstream candidate

Description snapshot: 2026-09-24

Strengthens recursive SQ ownership, mapping restoration and burst admission.

## Included work

- Checked alignment/ranges, bounded recursion, MMU-mode lifetime and migrated callers of the returned mapping pointer.

## Boundaries

- No fiber runtime, automatic MMU enabling or complete translated-buffer resolver.

## Dependencies and intended use

Independent prerequisite for the stereo PCM16 SQ upload correction.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/store-queue-safety.md](doc/store-queue-safety.md)
- [examples/dreamcast/basic/sq-safety/README.md](examples/dreamcast/basic/sq-safety/README.md)

Reviewed code snapshot: [`7e67e736fb41`](https://github.com/restricted-628/KallistiOS/commit/7e67e736fb41a6e1981502e70a8de22977495ed1).

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
