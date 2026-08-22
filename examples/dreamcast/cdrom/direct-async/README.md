# Asynchronous direct GD-ROM DMA validation

This example queues a sixteen-sector read through the direct GD-ROM transport
and verifies that it behaves as an ordinary KOS `cdrom_request_t`:

- submission returns a queued or running request;
- status identifies `CDROM_REQUEST_BACKEND_DIRECT`;
- terminal logical and physical byte counts are exact;
- the callback runs and observes coherent terminal state;
- the direct command and DMA completion events are retained;
- a following BIOS read matches every payload byte;
- both destination guards remain intact; and
- callback completion permits normal request destruction.

The example validates the raw direct-sector API independently of the optional
direct `/cd` backend. Run it from a bootable data image with Flycast's serial
console enabled or on hardware with a debug transport.

The 2026-08-16 Flycast interpreter run completed all 32,768 bytes, observed
both direct completion events, published coherent terminal callback state,
matched the following BIOS read byte-for-byte, preserved both guards, and
raised no SH-4 exception.
