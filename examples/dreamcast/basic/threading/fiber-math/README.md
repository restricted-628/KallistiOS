# Core fiber XMTRX probe

This example uses only KOS core fibers and matrix load/store operations.
It checks that the main continuation captures its pre-attachment XMTRX,
new child contexts start with identity, two children retain distinct values
across cooperative transfers, and automatic child return restores the main
matrix. All 16 entries are checked. It also calls `thd_pass()` within each child;
this alone does not prove a competing FPU-using thread was scheduled.

Build with `make` after loading the KOS environment. Runtime success prints
`KOSFIBERMATH children=2 rounds=2 xmtrx=16`.

The test does not claim fiber-local FPSCR, FPUL, exception modes, or arbitrary
assembly register banks. No decoder or SH4ZAM dependency is involved. A linked
binary is not an emulator or physical-hardware pass.
