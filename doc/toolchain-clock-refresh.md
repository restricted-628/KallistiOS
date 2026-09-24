# GCC 16.2.0 clock-library refresh

This refresh applies the libstdc++ clock support merged from upstream through
`caf8fbfa`. It does not change the SH-4 compiler version, target C standard,
floating-point ABI, KOS scheduler, or POSIX timer-stub behavior.

## Scope and build isolation

The installed compiler was compared with the current kos-chain patch set.
The GCC 16.2.0 target-patch delta is confined to libstdc++ configuration;
the KOS thread backend and compiler implementation are unchanged. The
replacement installation therefore reuses a separate copy of the existing
compiler, binutils and Newlib binaries and rebuilds libstdc++ from the same
GCC source release with the new patch. The original installation is retained.

Both `m4-single` and `m4-single-only` libraries are rebuilt. The default stays
`m4-single`. The normal Newlib fixup supplies KOS's current `machine/time.h`,
pthread and directory headers, and the KOS include link, in the candidate
prefix before configuration. The compiler executable remains byte-identical.

For a fresh installation, the complete kos-chain build remains the standard
route. A standalone library rebuild must reproduce its bootstrap environment:

- Newlib system headers must precede the installed C++ wrapper headers during
  configure checks, just as in the full GCC target-library build.
- The generated `libgcc/gthr-default.h` must select the KOS thread backend in
  each multilib build tree. Merely finding the correct thread-model name is
  insufficient to enable `_GLIBCXX_HAS_GTHREADS`.
- KOS's usual temporary `fake-kos.o` bootstrap symbols must be present for
  configure link checks and removed before any target validation or use.
- Preserve existing complex-number and static timezone configuration rather
  than treating a successful library build as sufficient evidence of parity.

The first standalone configuration was rejected before installation because
it failed those header/thread checks. The corrected generated configurations
preserve the previous settings for each ABI except the intended clock changes:

| Configuration | New state |
| --- | --- |
| `_GLIBCXX_USE_CLOCK_REALTIME` | Enabled |
| `_GLIBCXX_USE_CLOCK_MONOTONIC` | Enabled |
| `_GLIBCXX_USE_NANOSLEEP` | Enabled |
| `_GLIBCXX_USE_SCHED_YIELD` | Enabled |
| `_GLIBCXX_HAVE_SLEEP`, `_GLIBCXX_HAVE_USLEEP` | Fallback checks skipped because nanosleep is available |

Do not copy these macros into an old installed `c++config.h`. The clock
implementation in the compiled archive must be rebuilt as well.

## Acceptance checks

The [asset-free C++ probe](../examples/dreamcast/cpp/chrono_probe/) brackets
C++ realtime and monotonic readings with their corresponding KOS clock calls.
It also checks sleep duration, yield and thread creation/join. Its compile-time
guard deliberately rejects the previous library configuration.

Before selecting a candidate, verify its include/archive search paths, both
multilib configurations, absence of bootstrap symbols, and clock references
in the actual archive. Then rebuild KOS and run the probe plus representative
graphics/fiber examples. The upstream graphical clock additionally needs the
optional `libdcplib` port; that dependency is separate from libstdc++ itself.

Nanosecond units, non-whole-microsecond samples, and an emulator PASS do not
prove physical clock resolution or accuracy. This update enables the library
to consume KOS's finer clock readings; physical timing remains a separate gate.

### Recorded results (September 10, 2026)

- Both library ABIs built and installed successfully. Generated configuration
  differences are limited to the six settings listed above; thread support
  and each ABI's previous complex-number/timezone configuration are preserved.
- Neither installed `libgcc.a` contains the temporary `fake-kos.o` member.
  Defined global libstdc++ symbol-name comparisons found no removals in either
  ABI. This inventory is not a complete ABI-compatibility proof.
- The linked probe selects the candidate libstdc++ and this checkout's KOS
  archive. Disassembly of the rebuilt `chrono.o` confirms `clock_gettime`
  calls with the realtime/monotonic IDs and nanosecond arithmetic, without
  the old `gettimeofday` fallback.
- A clean default-ABI KOS build, including ARM firmware, passed. Existing
  host linker deprecation and ARM RWX-segment warnings remain.
- The asset-free C++23 probe passed in Flycast interpreter and dynarec:
  clock-domain brackets, nondecreasing readings, a 20 ms minimum sleep,
  yield, and C++ thread creation/join. The previous library correctly fails
  its compile-time feature guard.
- The same integer-only probe built against the `m4-single-only` library and
  passed in Flycast dynarec. KOS itself was built with the default `m4-single`
  ABI; this is not a complete alternate-ABI kernel validation.
- Rebuilt SH4ZAM camera/frustum/geometry/fiber integration passed in Flycast
  dynarec. The compound-material example rebuilt and linked successfully.
- The optional `libdcplib` port was installed from its official repository.
  The unmodified upstream graphical clock example now compiles and links;
  that result alone is not visual or physical-timing validation.

The local KOS environment now selects the validated installation. The previous
installation and a copy of the old environment are retained for rollback.
The toolchain installation is local build infrastructure, not part of the
source Git archive; the probe and this record are the tracked deliverables.

## Inherited module limitation

The previous GCC 16.2.0 build log and this rebuild both report missing C99
names when building the `std` module, followed by failure to build
`std.compat`. GCC falls back to empty module initialization objects and still
produces its ordinary library archive. This refresh does not claim to repair
that pre-existing standard-library module problem. Header-based C++23 support
and standard-library modules are separate validation targets.
