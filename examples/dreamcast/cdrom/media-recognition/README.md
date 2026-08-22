# BootROM media-recognition validation

This read-only diagnostic exercises KOS wrappers around the BootROM
media-recognition service and boot/current disc identity records. Dedicated
Dreamcast media is compared against `/1ST_READ.BIN`; ordinary CD media reports
that dedicated-disc identity checks were not applicable rather than claiming
they ran.
