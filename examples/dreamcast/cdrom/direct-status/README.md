# Direct GD-ROM status diagnostic

This example explicitly exercises the experimental direct GD-ROM PIO
transport. It performs a bounded `TEST_UNIT` / `REQ_ERROR` / `REQ_STAT`
readiness sequence without using the Dreamcast BIOS command server, then
prints the decoded drive state, sense key, ASC/ASCQ, and low-level task-file
observations.

The ISO9660 default in this fork is now direct; legacy BIOS APIs remain
available. This diagnostic is a low-level hardware-validation checkpoint; it
does not read sectors, enable direct DMA, or mount `/cd`.

Build with `make`, then load `gdrom-direct-status.elf` on one emulator or
console instance. The probe acknowledges CHECK with `REQ_ERROR`, retries
`REQ_STAT` once, and applies one deadline to the complete sequence. A no-disc
or unit-attention result is a diagnosed drive state rather than a transport
failure. On a transport failure, preserve the phase, ATA status/error,
interrupt reason, byte count, transferred byte count, and command counts from
the on-screen or debug-console trace.
