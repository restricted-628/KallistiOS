# Asynchronous CD request-engine validation

This program deliberately combines several advanced request-engine behaviors:
queue ordering, cancellation, callback dispatch, waiting for another request
from callback context, typed CDDA status, DMA completion, and media events. It
is an integration test rather than the smallest possible asynchronous-read
example.

Seek and typed CDDA status now default to direct SPI, while the legacy sector
read in this example explicitly uses `cdrom_bios_read_sectors_async`.
This deliberately mixed queue must
preserve ordering across both backends. Explicit BIOS-only callers can select
`cdrom_bios_seek_async` and `cdrom_bios_cdda_get_status_async`.

The cleanup helper is the important application pattern. It uses a finite wait;
on failure it requests cancellation and drains the request; it waits for any
queued callback; only then does it destroy the request. Partial submission is
handled with the same sequence so callbacks cannot retain stack-owned output.
