# Initial direct-disc default policy

Branch: `fork/direct-disc-default`

Role: Fork-policy snapshot

Description snapshot: 2026-09-24

Initial integrated-fork change selecting direct access for /cd and range/session constructors; retained as a policy topic.

## Included work

- Direct defaults for filesystem and high-level disc constructors.
- Explicit BIOS alternatives remain available.

## Boundaries

- This snapshot alone is not the completed generic cdrom_* migration and is not a clean upstream patch.

## Dependencies and intended use

Follow fork/direct-disc-convenience for the later routing topic, or master/the complete bundles for current combined behavior.

This branch includes integrated fork history. Do not submit its complete diff
as a single upstream kernel change.

## Source and detailed contracts

- [doc/disc-backend-defaults.md](doc/disc-backend-defaults.md)

Reviewed code snapshot: [`e75ac40305eb`](https://github.com/restricted-628/KallistiOS/commit/e75ac40305eb36abd8d5e87fc690e011b3176ba7).

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
