# VBlank priority and removal probe

This probe registers four VBlank handlers at different priorities, including
one handler through the legacy default-priority entry point. It verifies:

- ascending priority execution;
- registration-order stability at a shared priority;
- safe self-removal from interrupt context;
- continued dispatch of the remaining handlers on the next VBlank.

The program prints `VBLANK-PRIORITY: PASS` and exits when all checks pass.
Callbacks deliberately do no I/O and only update bounded probe state.
