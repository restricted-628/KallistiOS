# Direct GD-ROM status diagnostic

This example explicitly opts into the experimental direct GD-ROM PIO
transport. It performs a bounded `TEST_UNIT` / `REQ_ERROR` / `REQ_STAT`
readiness sequence without using the Dreamcast BIOS command server, then
prints the decoded drive state, sense key, ASC/ASCQ, and low-level task-file
observations.

The normal KOS CD-ROM path and default ISO9660 backend remain BIOS-backed. This diagnostic is
the first hardware-validation checkpoint for the future direct driver; it does
not read sectors, enable direct DMA, or replace `/cd`.

Build with `make`, then load `gdrom-direct-status.elf` on one emulator or
console instance. The probe acknowledges CHECK with `REQ_ERROR`, retries
`REQ_STAT` once, and applies one deadline to the complete sequence. A no-disc
or unit-attention result is a diagnosed drive state rather than a transport
failure. On a transport failure, preserve the phase, ATA status/error,
interrupt reason, byte count, transferred byte count, and command counts from
the on-screen or debug-console trace.
