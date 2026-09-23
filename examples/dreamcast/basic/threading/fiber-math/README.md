# Core fiber XMTRX probe

This example uses only KOS core fibers and matrix load/store operations.
It checks that the main continuation captures its pre-attachment XMTRX,
new child contexts start with identity, two children retain distinct values
across cooperative transfers, and automatic child return restores the main
matrix. All 16 entries are checked.

A separate KOS thread loads its own distinct XMTRX and exchanges semaphore
requests/completions with both children and their main continuation. Every
request changes its matrix before acknowledging completion. Eight exchanges
therefore prove the competing thread actually ran; this is not a bare yield
which could resume the same thread. Both threads verify their own matrix after
resumption. Waiting from a child blocks its whole owner thread, as documented.

Build with `make` after loading the KOS environment. Runtime success prints
`KOSFIBERMATH competing-thread=8` followed by
`KOSFIBERMATH children=2 rounds=2 xmtrx=16`. Assertions must remain enabled.

The test does not claim fiber-local FPSCR, FPUL, exception modes, or arbitrary
assembly register banks. No decoder or SH4ZAM dependency is involved. A linked
binary is not an emulator or physical-hardware pass. The exchanges exercise
explicit scheduler transitions; they do not measure arbitrary asynchronous
preemption timing or cover MMU-on execution.
