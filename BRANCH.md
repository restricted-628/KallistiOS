# Direct-default disc API and GAPS integration

Branch: `fork/direct-disc-convenience`

Role: Fork-policy snapshot

Description snapshot: 2026-09-24

Integrated direct-access routing and migration work, including explicit BIOS APIs and GAPS/BBA/loader ownership.

## Included work

- Generic disc status, reads, modes and playback/control routing; deliberate BIOS selection and streaming migration.
- G1-to-SRAM integration with owner/lease/transfer lifetime rules; low-density automatic mount/CDDA policy.

## Boundaries

- No automatic BIOS fallback, complete resident-loader hot switching, or promise of high-density GD-ROM data reading.

## Dependencies and intended use

Carries broad integrated history, not a narrow upstream PR. Extract mechanisms after ASIC/G1/G2 and filesystem/request prerequisites; keep default policy separate.

This branch includes integrated fork history. Do not submit its complete diff
as a single upstream kernel change.

## Source and detailed contracts

- [doc/disc-backend-defaults.md](doc/disc-backend-defaults.md)
- [doc/gaps-ownership.md](doc/gaps-ownership.md)

Reviewed code snapshot: [`2159cca42b48`](https://github.com/restricted-628/KallistiOS/commit/2159cca42b483c95a7c5d759ea275f07fe0a3c18).

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
