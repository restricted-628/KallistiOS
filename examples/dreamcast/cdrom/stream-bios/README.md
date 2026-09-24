# Explicit legacy BIOS streaming

This preserves the previous PIO/DMA comparison example with deliberately
named `cdrom_bios_stream_*`, BIOS TOC, and BIOS reference-read calls. Its
singleton state, firmware/IRQ callbacks, progress conventions and unbounded
waits are legacy BIOS behavior, not the direct staged-session contract.

For new direct applications, use `../stream`. This BIOS diagnostic requires
suitable data media and hardware/emulator I/O validation; it is not a no-media
regression probe or a model for bounded production cleanup.
