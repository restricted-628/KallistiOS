# C++ clock toolchain probe

Build with a KOS environment loaded using `make`, then load `chrono-probe.elf`
or use `make run`. No font library or external assets are required. Use
`make clean` before switching compilers; object timestamps do not identify
which installed toolchain built them.

This requires libstdc++ rebuilt with the KOS clock configuration patches and
the Newlib time-header fixup. Older libraries deliberately fail the compile
check instead of producing a misleading successful clock demonstration.

The runtime brackets `system_clock`/`steady_clock` readings with the matching
KOS C clocks. This checks both their epochs and their returned values; a
nanosecond `duration` typedef alone cannot prove the underlying implementation
uses `clock_gettime`. It also exercises C++ sleep/yield and checks that sleep
does not finish before its requested deadline. A thread creation/join check
guards against accidentally disabling KOS gthread support during a clock-only
library rebuild. Run without an application changing the realtime clock
concurrently.

Non-whole-microsecond readings are reported, not required. Neither those
samples nor `clock_getres()` establish physical timer accuracy or resolution.
Pair emulator runs with inspection of the linked clock implementation and
physical-console timing measurements before making such claims.
