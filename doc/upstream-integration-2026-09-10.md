# Upstream integration: September 10, 2026

Merged official KallistiOS master from `33c6e0ba` through
`caf8fbfa8464af5701e000b47c2bb910055a3206` (nine commits) into the existing
graphics branch. A normal merge preserves published history and records the
complete upstream ancestry; no rebase or selective omission was necessary.

## Integration decisions

- Adopt upstream's volatile cache-control assembly and read/write operands
  for purge/write-back, retaining this branch's P2-to-P1 alias conversion and
  overflow-safe range traversal. These changes complement one another.
- Adopt compiler memory barriers around IRQ mask changes. This benefits
  existing driver, request, fiber and graphics critical sections without
  changing their scheduling or ownership model.
- Adopt nearest-preceding-symbol address lookup, retaining reference-protected
  name-manager traversal. The only textual conflict was its header comment:
  retain both the new lookup description and the borrowed-result lifetime
  warning. Extend the lifecycle test with unsorted/high-address tables,
  below-first/exact/between-symbol queries and reference-release checks.
- Adopt `timer_gettimespec()` for the KOS uptime helper. The POSIX
  `timer_gettime(timer_t, struct itimerspec *)` now owns the old name; do not
  introduce a compatibility macro that collides with it. No branch-specific
  call sites needed conversion.
- Retain upstream's POSIX timer stubs as stubs. Advertising clock APIs to
  Newlib does not implement asynchronous POSIX timer delivery or replace this
  branch's existing timer/fiber services.

## Installed toolchain versus source patches

The new GCC patches enable libstdc++ clock/nanosleep/scheduler support during
configuration. Merely merging those patches does not update an installed
compiler library. The currently installed GCC 16.2.0 still has the relevant
`_GLIBCXX_USE_CLOCK_*`, `NANOSLEEP`, and `SCHED_YIELD` settings undefined, and
its sysroot still has Newlib's previous generic `machine/time.h`.

Next toolchain step: use the updated kos-chain patches and Newlib header fixup
to build a separately validated GCC 16.2.0 toolchain prefix, with the KOS time
header installed before configuring the final libstdc++ build. Verify its
configuration, clock call path and target timing before switching the shared
environment. Do not hand-edit installed `c++config.h`: that cannot rebuild
the library's already compiled clock implementation. No GCC major-version
upgrade or global C-language-standard change is required by this merge.

## Validation

- Clean GCC 16.2.0 KOS build, including ARM firmware: passed. Existing host
  linker `-s` deprecation and ARM RWX-segment warnings remain.
- Name-manager/export and G1 suites: passed GCC 14 GNU17/strict C23 and
  Apple Clang GNU17/strict C2x (eight focused runs).
- Name-manager/export suite: passed AddressSanitizer/UndefinedBehaviorSanitizer.
- Optimized SH-4 assembly probe: stores feeding cache write-back/purge remain
  before their instructions, and ordinary stores remain on the correct sides
  of IRQ disable/restore. This is compiler evidence, not physical cache proof.
- Cache-safety, SH4ZAM integration and compound-material examples rebuilt.
- The rebuilt SH4ZAM camera/frustum/geometry/fiber integration fixture passed
  in Flycast dynarec. Physical performance and cache behavior remain untested.
- Standalone C++23 time probe passed in Flycast dynarec: renamed uptime API,
  POSIX stub errors and basic chrono use. No improved chrono precision is
  claimed for the old installed library.
- Upstream C++ clock example could not build because the optional dcplib font
  header is absent. Its source was retained unchanged; it still needs that
  dependency and validation with the refreshed toolchain.

This checkpoint does not close the physical graphics/cache validation gates
or claim a complete rerun of every unrelated host suite.
