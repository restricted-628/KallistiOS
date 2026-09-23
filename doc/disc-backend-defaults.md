# Direct disc defaults (fork policy)

The default paths in this first policy change are:

| Entry point | Default | Explicit BIOS selection |
| --- | --- | --- |
| `/cd` reads, metadata, async reads, staged sessions, preseek | Direct SPI/Holly | `fs_iso9660_set_backend(FS_ISO9660_BACKEND_BIOS)` before the first mount attempt |
| Cached media sampling | Direct SPI | Follows the `/cd` selection |
| `cdrom_sector_range_open` | Direct, 2048-byte Mode-1 sectors | `cdrom_bios_sector_range_open` |
| `cdrom_stream_session_start` | Direct, 2048-byte Mode-1 sectors | `cdrom_bios_stream_session_start` |

The raw constructors do not inherit the filesystem's selection or the BIOS
sector-size setting. Use the format-selecting `gdrom_direct_*` constructors
for Mode-2 Form-1. Direct staged sessions require nonzero startup and idle
timeouts and a sector count from 1 to 65535. The named BIOS constructor retains
the previous sector-size and zero-startup-timeout behavior.

There is no automatic fallback to the BIOS server after a direct failure.
Backend selection for `/cd` locks on the first mount attempt, as before.
Shutting down and reinitializing the filesystem restores the direct default.
Enum values are unchanged; zero still means an explicit BIOS selection, not
"use the default".

## Compatibility work still outstanding

This is not yet a universal rerouting of all `cdrom_*` functions. Legacy raw
BIOS-command submission, reinitialization/sector-mode control, synchronous
reads, legacy streams, and their BIOS request helpers retain their current
contracts. They require a separate compatibility/routing pass before the fork
can claim that every generic convenience API defaults to direct. Applications
needing the new direct behavior should use the default paths above or the
explicit `gdrom_direct_*` API in the meantime.

Boot-time drive authorization/initialization is also unchanged. Direct runtime
I/O is not a replacement for the boot ROM. The direct transport remains
experimental pending physical-drive timing, recovery, and media validation;
making it the default does not prove those properties.

## Validation

`examples/dreamcast/cdrom/backend-defaults` checks the filesystem default,
explicit BIOS selection, initialization reset, range backend identity, and
staged-constructor dispatch. Link-time spies replace session submission so
this probe issues no staged read and claims no physical transport validation.
`direct-iso9660` now tests `/cd` without explicitly selecting direct first; its
full I/O test requires a suitable self-boot disc image.

For this patch, the full SH-4 GCC 16.2 kernel/addon build and the three affected
example links passed. The 14-check routing probe passed with Flycast v2.7 in
both interpreter and dynarec modes. The pure SPI packet tests passed with GCC
14 GNU17/C23 and Clang ASan/UBSan. The full self-boot ISO9660 I/O test and
physical-drive tests were not run for this policy change.
