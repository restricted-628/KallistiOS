# PVR buffered multipass

This example assigns independent, double-buffered vertex staging memory to
three hardware registration passes. The DMA and list-completion interrupts
alternate through all three passes before one renderer submission.

The expected image contains blue, green, and red vertical panels. The example
also verifies that early hybrid list flushing remains rejected until its
pass-ownership contract is implemented. It prints
`RESULT: PASS (PVR buffered multipass)` after 120 frames.
