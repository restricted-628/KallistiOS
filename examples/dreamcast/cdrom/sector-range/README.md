# Bounded sector-range validation

This program validates the KOS sector-oriented cursor abstraction over both
the BIOS and direct GD-ROM transports. It covers synchronous and asynchronous
reads, seek/tell/EOF, preseek, a 33-sector chained read, and staged streaming.

The test expects a readable data track with at least 64 sectors starting at the
selected FAD. Direct-backend portions remain post-boot hardware validation and
do not replace BootROM GD authorization.
