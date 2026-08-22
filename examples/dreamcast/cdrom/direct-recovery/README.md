# Direct GD-ROM recovery diagnostic

This deliberately destructive controller test validates the two failure paths
that the normal direct-read comparison cannot reach:

- force-stop an active GD-DMA and prove a direct PIO read still works;
- exclude a valid destination from `SB_GDAPRO` and require Holly's GD-DMA
  illegal-address or overrun event;
- perform a normal direct DMA afterward and compare it byte-for-byte with a
  BIOS-backed read; and
- verify that neither destination guard is modified.

The diagnostic never accepts a caller-selected invalid physical address. The
protection case changes only the protection window around an otherwise valid,
aligned system-RAM destination.

Run this example by itself from a bootable data image. Do not incorporate it
into application startup. A result of `ENOTSUP` means the emulator completed
the deliberately excluded transfer instead of enforcing/reporting
`SB_GDAPRO`; the example still reports whether the final normal DMA and BIOS
read remained usable. Real hardware validation remains required even after an
emulator pass.
