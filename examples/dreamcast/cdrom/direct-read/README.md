# Direct GD-ROM BIOS/PIO/DMA sector-read validation

This example locates the final data track through the low-density TOC, selects
the appropriate cooked 2048-byte data format, then compares its first two
sectors through the experimental
direct SPI PIO path, the direct Holly GD-DMA path, and KOS's BIOS-backed path.
It verifies every payload byte, all three guard regions, both completion
domains, the exact final DMA byte count, and successful return to BIOS access.
The framebuffer shows a compact result and direct task-file trace.

Run it from a bootable data image, not over dcload with an empty virtual drive.
When packaging it as a multi-track emulator image, store the KOS executable in
the ISO image unscrambled as required by that test layout.

Passing in Flycast proves normal direct `CD_READ` packet flow, repeated PIO DRQ
groups, Holly DMA completion, exact-length checks, and return to the BIOS-backed
path in the emulator. Forced timeout/reset recovery, DMA protection faults, and
physical CD-R timing remain separate hardware tests.
