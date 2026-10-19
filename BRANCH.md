# Complete SH4ZAM fiber and Service Executor SDK

Branch: `addon/fiber-service-sh4zam`

Role: Complete SDK bundle

Description snapshot: 2026-10-18

One integrated checkout whose sole fiber provider is libfiber_sh4zam, including the Fiber Service Executor.

## Included work

- Addon-owned context switching, synchronization, runtime and executor; accelerator operations use SH4ZAM directly.
- Direct-disc/G1/G2/GAPS support and application-fiber RAM/VRAM and deferred VBlank examples.

## Boundaries

- No simultaneously linked KOS core fiber implementation or KOS-math fallback for this provider. Not a standalone kos-ports release.

## Dependencies and intended use

Mutually exclusive with pr/core-fibers. The checkout already contains its prerequisites; do not assemble it from multiple branches.

This branch includes integrated fork history. Do not submit its complete diff
as a single upstream kernel change.

## Source and detailed contracts

- [FIBER-BUNDLE.md](FIBER-BUNDLE.md)
- [addons/libfiber_sh4zam/README.md](addons/libfiber_sh4zam/README.md)
- [doc/fiber-bundle-validation.md](doc/fiber-bundle-validation.md)

Refresh baseline: integrated master
[`cf53f5c5a18e`](https://github.com/restricted-628/KallistiOS/commit/cf53f5c5a18e78ab7e5b79455675d1ba70b22647),
including accepted VM1–VM3 and shared storage fixes. VM4 is not included.

Common ancestor with the inspected upstream master:
[`55da82831f4c`](https://github.com/restricted-628/KallistiOS/commit/55da82831f4cadef233b02c95493011428fb917f).
This records the inspected baseline, not a claim of being rebased to today's upstream.

Follow the validation record for exact tested revisions and limits. Updating
this SDK does not certify every inherited feature; physical hardware validation
remains a separate gate. Private research and uncommitted experiments are not
part of the bundle.

[All published branches](https://github.com/restricted-628/KallistiOS/blob/master/BRANCHES.md) ·
[Integrated fork differences](https://github.com/restricted-628/KallistiOS/blob/master/FORK.md) ·
[Official KallistiOS](https://github.com/KallistiOS/KallistiOS)
