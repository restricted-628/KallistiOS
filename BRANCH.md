# Shared G1 bus ownership

Branch: `pr/g1-bus-ownership`

Role: Stacked upstream candidate

Description snapshot: 2026-09-24

Centralizes ownership of the G1 controller shared by disc and ATA paths.

## Included work

- BIOS/ATA arbitration, fault-event routing, timeout/abort ownership and IRQ restoration.

## Boundaries

- No complete direct-disc driver/default-policy series or full GAPS/BBA lifecycle extraction.

## Dependencies and intended use

Includes the ASIC-event ownership prerequisite. Review that mechanism first.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/g1-bus-ownership.md](doc/g1-bus-ownership.md)
- [doc/asic-event-ownership.md](doc/asic-event-ownership.md)

Reviewed code snapshot: [`1fd31512d630`](https://github.com/restricted-628/KallistiOS/commit/1fd31512d63098e4852723c7c94ecf98c4f72549).

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
