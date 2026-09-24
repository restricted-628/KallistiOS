# Page-table lifecycle host test

Run `make test CC=gcc-14` or `make test CC=clang`. Override `CFLAGS` for strict
C23 (`-std=c23` on GCC, `-std=c2x` on older Clang) or ASan/UBSan.
Keep assertions enabled; `-DNDEBUG` is not a supported test configuration.

`run.py` takes a verbatim section of production `mmu.c`, includes the real
`arch/mmu.h`, and compiles it with `test.c`. It substitutes only allocation,
IRQ exclusion, PTEH/MMUCR storage, TLB loads, and cache/TLB retirement hooks. It neither copies
the mapping algorithms nor claims to emulate the CPU or allocator latency.

Checks cover three failure points when extending across missing tables plus
context allocation failure, existing-table no-allocation updates, exact
retirement events, invalid arguments, range endpoints, precompiled PTE words,
cache-update rejection across a hole, empty-table reclamation, inactive/active
destruction, static-map validation/capacity, and legacy void source signatures.

`--source PATH` selects an alternate production file for negative controls.
No private references, special host packages, target hardware or emulator are
needed. See `doc/mmu-page-lifetime.md` for separate target validation gates.
