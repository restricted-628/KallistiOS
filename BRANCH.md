# SH4ZAM specular-lighting policy

Branch: `fork/sh4zam-specular`

Role: Fork-policy snapshot

Description snapshot: 2026-09-24

Uses SH4ZAM fast power in the fork's specular-lighting path; retained as an integrated graphics policy topic.

## Included work

- Direct shz_powf use for specular lighting and associated regression expectations.

## Boundaries

- Not a general replacement for accurate libm powf and not a claim of measured console speed or visual equivalence.

## Dependencies and intended use

Already incorporated into integrated development. This broad fork snapshot is not suitable for wholesale upstream submission.

This branch includes integrated fork history. Do not submit its complete diff
as a single upstream kernel change.

## Source and detailed contracts

- [doc/sh4zam-direct-graphics-math.md](doc/sh4zam-direct-graphics-math.md)

Reviewed code snapshot: [`d247d657a66a`](https://github.com/restricted-628/KallistiOS/commit/d247d657a66a783e77dc3f78477f3eb146da9927).

Common ancestor with the inspected upstream master:
[`fcfa7d869471`](https://github.com/restricted-628/KallistiOS/commit/fcfa7d869471591ca1c777543261a7bfea7cb726).
This records the inspected baseline, not a claim of being rebased to today's upstream.

The description pass changes documentation only. It does not rerun code tests,
prove hardware behavior, or certify every inherited feature. Follow the linked
contracts and reproduce the relevant host, target and emulator checks; physical
hardware validation remains a separate gate. Private research and uncommitted
experiments are not part of this description.

[All published branches](https://github.com/restricted-628/KallistiOS/blob/master/BRANCHES.md) ·
[Integrated fork differences](https://github.com/restricted-628/KallistiOS/blob/master/FORK.md) ·
[Official KallistiOS](https://github.com/KallistiOS/KallistiOS)
