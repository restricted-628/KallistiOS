# Caller-owned matrix stack

This example builds a two-level transform hierarchy using a four-entry matrix
stack supplied by the application. It verifies that a child transform can be
removed without disturbing its parent, that a saved level can be restored
without being consumed, and that underflow and overflow are reported without
changing the current matrix.

The stack allocates no memory and creates no background service. Each stack is
owned by the execution context that uses it. KOS threads retain their own SH-4
matrix-register state across scheduling; cooperative fibers on one carrier
thread share that register and should explicitly restore a saved level after a
fiber transfer when needed.

Successful completion prints and displays
`RESULT: PASS (caller-owned matrix stack)`, then leaves the result visible.
