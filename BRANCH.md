# Graphics addon extraction workspace

Branch: `addon/graphics-extraction`

Role: Extraction in progress

Description snapshot: 2026-09-24

Ownership manifest and audit for a future shared 2D/3D graphics addon, not a finished independent library.

## Included work

- Proposed ownership split for Compact assets, scenes, animation, deformation, lighting, cells, particles and related tools/tests.
- Explicit separation gates for retained PVR hardware contracts and higher-level graphics policy.

## Boundaries

- The selected implementation is still in libkallisti. There is no completed libdcgfx archive or published kos-ports recipe.

## Dependencies and intended use

Integrated fork snapshot. Resolve private PVR dependencies, move matching headers/tools/tests, and establish single-archive ownership before packaging.

This branch includes integrated fork history. Do not submit its complete diff
as a single upstream kernel change.

## Source and detailed contracts

- [addons/libdcgfx/README.md](addons/libdcgfx/README.md)
- [addons/libdcgfx/sources.mk](addons/libdcgfx/sources.mk)
- [utils/addon-extraction/audit.py](utils/addon-extraction/audit.py)

Reviewed code snapshot: [`72c2cb5acea2`](https://github.com/restricted-628/KallistiOS/commit/72c2cb5acea242631527ee50595ced165f7553d7).

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
