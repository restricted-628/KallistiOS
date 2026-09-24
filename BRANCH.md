# Integrated experimental KallistiOS fork

Branch: `master`

Role: Integrated SDK

Description snapshot: 2026-09-24

Development home for the combined OS, direct-I/O, graphics and service work. This is not the official KallistiOS master branch or a single upstream pull request.

## Included work

- Kernel ownership/lifetime fixes and core fibers with integrated Service Executor support.
- Direct-default disc APIs, G1/G2/GAPS integration, and optional application-fiber adapters.
- Compact 2D/3D graphics and direct SH4ZAM math integration; bounded LZ4/ADX work.

## Boundaries

- Completed graphics-addon extraction and blanket physical-hardware validation are not claimed.

## Dependencies and intended use

Use one checkout for integrated development. Choose a complete fiber bundle when provider exclusivity is required.

This branch includes integrated fork history. Do not submit its complete diff
as a single upstream kernel change.

## Source and detailed contracts

- [FORK.md](FORK.md)
- [BRANCHES.md](BRANCHES.md)
- [doc/fiber-runtime.md](doc/fiber-runtime.md)
- [doc/disc-backend-defaults.md](doc/disc-backend-defaults.md)

Reviewed code snapshot: [`163e4a5b635e`](https://github.com/restricted-628/KallistiOS/commit/163e4a5b635eb106b7c4d49597b15aca022da231).

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
