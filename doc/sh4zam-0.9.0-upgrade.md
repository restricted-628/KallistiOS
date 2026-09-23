# SH4ZAM 0.9.0 integration

Date: 2026-09-22.

## Dependency and scope

- Official release: https://github.com/gyrovorbis/sh4zam/releases/tag/v0.9.0
- Commit: `0fd3a1e1fa0809d33198c062632b1494ec2f57df`.
- Annotated tag object: `967d72a783a0bf9ba92d8cb43c96ad3c28254345`.
- Replaces temporary PR #70 commit `0c1ccb5f5614314e36e2fec3179c8ca3ae844770`.
- Submodule URL restored to `https://github.com/gyrovorbis/sh4zam.git`.
- Official source, history, author notices, and license remain intact. The
  temporary fork exception was removed from the provenance checker.

Upstream moved its v0.8.1 tag after our original integration. Historical test
reports refer to the exact commit `be71e8a1428374498e8f5e7506c7e244375f4399`;
they do not describe the subsequently retagged revision. No existing local
historical tag was forcibly replaced during this upgrade.

This is a dependency upgrade, not a graphics/addon extraction or an MMU policy
change. Existing pending MMU/ADX work was left untouched. No proprietary
reference material or reverse-engineering output is included.

## Integration decisions

The release adds compact XMTRX 3x4 APIs and changes SH-4 matrix transforms,
scale extraction, and u16 trigonometry. Runtime SH-4 u16 calls now use inline
FSCA even under fast-math; portable and constant-input paths use 65536 units
per turn. The earlier KOS radians helper remains source-compatible, but new
callers may use `shz_sincosu16()` directly.

The addon source list and TLS model are unchanged. The FFT translation unit
is unchanged, so its existing GCC 16.2 `-O2` build workaround remains. No
upstream assembly constraints or memory clobbers were patched locally.

## Regression coverage

- Existing camera/frustum, animation, composition aliasing, skinning, Compact
  draw, wire, toon/outline, modifier, and two-volume fixtures are retained.
- Header and linked-library version must both identify 0.9.0.
- Non-affine matrix scale checks put nonzero values in W lanes, exercising
  the release's scale-extraction fix. One-off vec3 transforms also verify
  that XMTRX remains unchanged.
- Asymmetric compact matrices are checked against an independent scalar
  4x4 product oracle: normal/transposed loads, transposed stores, column loads,
  forward/reverse and transposed products, fused load/apply and load/apply/store.
- `utils/sh4zam-release-test` reuses these target fixtures on the software
  backend and is discovered by the common host-test runner.
- `u16-trig-test.elf` tests every angle against strict double-precision trig
  for both upstream and KOS helper APIs in C strict/fast-math/Ofast and C++
  fast-math modes. The direct API tolerance is `1e-6`, which rejects the old
  65535-unit convention; the compatibility helper retains `3e-4` for its
  radians-to-FSCA quantization. Each lane also checks a constant quarter-turn.
- The original cardinal-angle fast-math reproducer remains separate.

## Validation

- Full forced KOS/addon rebuild and all five integration executables:
  SH-4 GCC 16.2.0, GNU17. Passed. Build warnings were the existing host linker
  obsolete `-s` warnings and AICA firmware RWX-segment warning.
- Source provenance: 11 offline tests passed; real checkout clean at the
  pinned official revision, with matching license and public header symlink.
  A fresh empty repository fetched the exact commit from the official URL.
- Existing complete host runner: GCC 14 GNU17, 68/68 suites passed. The new
  release suite was added after that runner enumerated its tests and was
  validated separately in all four compiler/language lanes below.
- Release fixtures: GCC 14 GNU17/C23 and Apple Clang GNU17/C2x passed with
  warnings as errors; Clang ASan/UBSan passed separately.
- Full-angle host sweep: GCC 14 and Apple Clang passed all eight lanes.
  Largest observed component error: `5.39436868e-7`.
- C++ 3x4 API compile probes cover const operands and a temporary operand,
  transpose, reverse apply, and transpose store: SH-4 GCC 16.2/GCC 14 GNU++23
  and Apple Clang GNU++20 passed. This is compile coverage, not a separate
  runtime suite for the C++ wrappers. The C API's C++17 trig lanes above are
  distinct from the higher language requirements of the `.hpp` wrapper API.
- Flycast interpreter and dynarec: main integration fixture, original direct
  fast-math reproducer, and eight-lane full-angle sweep all passed.
  Largest observed direct-u16 component error: `1.12067502e-7`; compatibility
  helper: `9.58878754e-5`. These are observations, not universal error bounds.

Emulator validation used a separate local strict-MMU-capable Flycast build
based on `628bd3dbb160ea2750230fc6b00c0bb8173cb1f6`, with private test settings,
16 MiB RAM, and the checkout's pending default-MMU startup enabled. Installed
Flycast and normal user settings were not modified. No physical Dreamcast
validation or performance measurement is claimed, and no Sofdec compatibility
conclusion follows from these tests.

## Reproduction

```sh
git submodule sync -- addons/libsh4zam/upstream
git submodule update --init --recursive
python3 utils/check-sh4zam-source.py
python3 utils/test-check-sh4zam-source.py
source environ.sh
make -B -j4
make -C examples/dreamcast/sh4zam/integration -B -j4
make -C utils/sh4zam-release-test -B CC=gcc-14 test
```

Run `sh4zam-integration.elf`, `fast-trig-probe.elf`, and `u16-trig-test.elf`
separately with serial output and verify each `RESULT: PASS` line. See the
integration example README for host sweep commands and expected output.
