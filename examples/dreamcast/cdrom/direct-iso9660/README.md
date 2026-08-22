# Direct ISO9660 backend validation

This example selects `FS_ISO9660_BACKEND_DIRECT` before the first `/cd`
operation and proves that normal KOS filesystem APIs run over the direct SPI
and Holly GD-DMA implementation:

- the mount probe, TOC discovery, and ISO9660 metadata reads complete;
- the shared media monitor publishes a direct-backend drive-state sample;
- descriptor-aware pickup preseek completes through direct `CD_SEEK`;
- a 32 KiB synchronous `fs_read()` succeeds without BIOS data commands;
- a whole-sector asynchronous read uses a direct-backend request;
- one direct SPI `CD_READ2` session is drained by two ordinary asynchronous
  transfer requests and matches the synchronous payload;
- an intentionally abandoned direct session reaches its mandatory idle
  timeout, performs bounded cleanup, and leaves the drive reusable;
- an unaligned-position, unaligned-destination byte read uses the existing
  ISO9660 bounce planner over direct DMA and matches synchronous data;
- zero-length asynchronous completion retains direct backend identity;
- switching backends after mount is rejected with `EBUSY`; and
- destination guards remain intact.

Build the program, place its scrambled binary in a self-boot data image as
`1ST_READ.BIN`, and run that image in Flycast or on a Dreamcast. The test reads
its own file through `/cd`, so no extra payload is required.

The BIOS backend remains KOS's default; this test opts in deliberately before
opening `/cd/1ST_READ.BIN`.
