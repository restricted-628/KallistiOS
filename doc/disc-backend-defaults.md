# Direct disc defaults (fork policy)

The direct-default paths implemented so far are:

| Entry point | Default | Explicit BIOS selection |
| --- | --- | --- |
| `/cd` reads, metadata, async reads, staged sessions, preseek | Direct SPI/Holly | `fs_iso9660_set_backend(FS_ISO9660_BACKEND_BIOS)` before the first mount attempt |
| Cached media sampling | Direct SPI | Follows the `/cd` selection |
| `cdrom_sector_range_open` | Direct, 2048-byte Mode-1 sectors | `cdrom_bios_sector_range_open` |
| `cdrom_stream_session_start` | Direct, 2048-byte Mode-1 sectors | `cdrom_bios_stream_session_start` |
| `cdrom_seek_async` | Direct SPI seek | `cdrom_bios_seek_async` |
| `cdrom_cdda_get_status`, `cdrom_cdda_get_status_async` | Direct SPI Q-channel query | `cdrom_bios_cdda_get_status`, `cdrom_bios_cdda_get_status_async` |
| `cdrom_get_status`, `cdrom_read_toc` | Direct SPI status/TOC query | `cdrom_bios_get_status`, `cdrom_bios_read_toc` |

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

Seek and typed CDDA queries likewise do not inherit `/cd` selection. Direct
async calls require a nonzero timeout; the explicit BIOS calls retain zero
for no timeout. The synchronous typed query uses a 10000 ms primary-command
timeout (bounded recovery may take additional time) and preserves the common
`ERR_*` return vocabulary, including atomically captured drive sense. Use
`gdrom_direct_cdda_get_status` when a custom timeout or raw trace is needed.
The BIOS-only internal seek helpers used by explicitly selected BIOS ranges
and filesystem descriptors are unchanged.

Drive status and TOC also use a 10000 ms primary-command timeout and remain
independent of `/cd` selection. Status preserves its actual legacy `0`/`-1`
return convention (not the `ERR_*` enum), permits either output to be NULL,
and sets supplied outputs to -1 on failure, even if the direct diagnostic
transport decoded a payload before CHECK. It requires thread context; the
bounded G1 command path rejects interrupt-context calls with `EPERM`.
TOC retains `ERR_*` results and both density-area choices. The explicit BIOS
filesystem mount and BIOS-reference/reuse examples call the named BIOS
versions, so their selected transport is not changed by these defaults.

## Compatibility work still outstanding

The BIOS side of sector reads and format selection now has explicit names:
`cdrom_bios_read_sectors`, `cdrom_bios_read_sectors_ex`,
`cdrom_bios_read_sectors_async`, `cdrom_bios_change_datatype`,
`cdrom_bios_reinit`, `cdrom_bios_reinit_ex`, and `cdrom_bios_set_sector_size`.
Their generic counterparts remain BIOS compatibility aliases in this step;
this is not a direct-read default switch. Boot setup, BIOS filesystem/range
paths, and BIOS read-reference examples use the explicit functions so later
generic routing cannot silently change their selected backend.

BIOS sector-size bookkeeping now changes only after the firmware accepts a
mode update. Failed automatic track-type queries stop before submitting any
mode change. Callers must still serialize mode changes against outstanding
BIOS reads/streams, including requests waiting in the queue; command-level G1
ownership does not make global sector-mode changes safe for queued reads.

Before generic reads can switch, the direct path needs an explicit format
contract for cooked and raw sectors, a policy for legacy automatic selection,
and compatible arbitrary-count DMA handling. Queued reads
must capture their format rather than depend on a later global mode change.
The synchronous `gdrom_direct_read_sectors` PIO entry point now also accepts
`GDROM_DIRECT_SECTOR_RAW2352`: complete 2352-byte sectors without subchannel
data, into a two-byte-aligned destination. Large PIO reads are split into
commands of at most sixteen sectors, including odd raw tails, with one
absolute deadline and G1 release between commands. Format selection
is explicit per call, independent of the BIOS mode. Short transfers fail with
`EPROTO`; excess data is drained without overrunning the destination and fails
with `EMSGSIZE`. No commands after the failure are issued; earlier output
is retained and the transport record accumulates the transferred byte count.
Other diagnostic fields describe the last command attempted. Buffer-size,
pointer-wrap, and FAD-span validation precede all I/O. There is no allocation
or automatic backend switch. This does not change the generic BIOS aliases.

Direct DMA accepts RAW2352 for even counts, with a
32-byte-aligned destination and exact sectors * 2352 byte accounting. This
applies to synchronous/queued RAM or PVR destinations and leased GAPS SRAM.
The queued executor captures format and byte counts at submission and uses
that same size for the execution-time lease claim. GAPS operations remain
limited to sixteen sectors. Synchronous RAM/PVR reads split larger ranges into
bounded commands without allocating queue or staging state. The full range is
validated before I/O; one absolute deadline covers the read and G1 waits.
G1 is released between commands, so other disc operations may interleave.
The result's `transferred` field is cumulative (including partial failures),
while the remaining fields describe the last attempted command. No later
command is issued after failure. Normal queued RAM/PVR reads can
span multiple commands: each is limited to sixteen sectors and requeued at
the tail afterward. One chain deadline starts at first execution and includes
time spent waiting between segments. Initial queue residence is not charged.
The complete destination and FAD span are checked at admission. One small
metadata record is reclaimed before terminal publication; no payload staging
or per-segment allocation is used. Request status is cumulative; an optional
transport trace describes the last hardware command executed. Two raw sectors occupy
4704 bytes, an exact multiple of 32; odd counts are rejected before submission
or G1 ownership. There is no padding, extra sector read, hidden temporary
buffer, or PIO fallback. Explicit PIO remains available for odd raw counts.

Ranges and staged sessions remain cooked-only. The internal direct-chain
constructor now accepts raw pairs and verifies every segment's exact wire
count against its byte accounting, including requeued segments.
Arbitrary-count raw DMA/staging still needs a separate contract before generic
reads can switch. Generic BIOS APIs accept
configured raw layouts, which must not be silently reinterpreted as cooked.

This is not yet a universal rerouting of all `cdrom_*` functions. Legacy raw
BIOS-command submission, reinitialization/sector-mode control, sector reads
(sync and async), raw subcode, playback controls, legacy streams, and their
BIOS request helpers retain their current contracts. They require a separate
compatibility/routing pass before the fork
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

The follow-up seek/typed-status routing pass adds
`examples/dreamcast/cdrom/convenience-routing`. All 54 checks pass in both
Flycast interpreter and dynarec modes, including explicit BIOS paths,
submission failures without fallback, Q-channel decoding, and error/sense
translation. The 14-check default probe and four-case G1 ownership probe also
pass in both modes after relinking. The full SH-4 GCC 16.2 build, both affected
driver units with `-Werror`, and the request/CDDA-status example builds pass.
These routing tests use transport spies; live mixed-backend playback and
physical-drive behavior remain untested in this pass.

The subsequent status/TOC pass expands that probe to 128 checks, passing in
both Flycast modes. The original defaults and G1 ownership probes still pass
in both modes. The full SH-4 build, `cdrom.c` and `fs_iso9660.c` with `-Werror`,
and 14 disc example rebuilds pass. Named BIOS exports are generated in both
the symbol table and export stubs. Actual ISO9660 mount/read and live metadata
transport behavior were not exercised by these transport-spy tests.

The BIOS read/mode boundary pass adds `bios-read-contract`: 151 checks pass in
both Flycast modes, covering the explicit APIs and temporary generic aliases,
failed mode/query state preservation, cooked/raw async byte accounting,
17-sector submissions, and BIOS reinitialization. The 128-check convenience,
14-check defaults, and four-case G1 probes also pass in both modes. The full
SH-4 build, three affected driver units under `-Werror`, and ten existing disc
example rebuilds pass; the new probe also builds and links. Firmware and queue
spies mean this does not prove actual raw data transfer or DMA completion.

The raw PIO pass adds `direct-raw-pio`, compiling the production direct driver
with substituted MMIO accesses. Its 90 checks pass in both Flycast modes:
cooked/raw packets, multi-phase transfers, exact byte counts, short/oversized
responses with destination guards, invalid-input rejection, and G1 release.
DMA/range/session rejection of the PIO-only format is also covered. The full
SH-4 GCC 16.2 build and the direct driver under `-Werror` pass, as do the SPI
packet tests under GCC 14 GNU17/strict C23 and Clang ASan/UBSan. These simulated
register tests do not prove physical raw-sector contents or drive timing.
After relinking, the BIOS read, convenience-routing, default-selection, and
G1 ownership probes still pass all 151/128/14/4 checks in both Flycast modes.

The even-count raw DMA pass adds `direct-raw-dma`. Its 1426 checks pass in
both Flycast modes using the production driver with simulated registers,
cache operations, IRQ clients, queue submission, and SRAM leases. Coverage
includes every even raw count from 2 through 16, both completion-event orders,
exact byte/protection/cache ranges, cooked-format preservation, short-DMA
failure, odd/invalid input rejection, memory limits, deferred execution,
pre-command cancellation, progress, and SRAM lease sizing/failure/release.
The full SH-4 GCC 16.2 build, direct driver and new probe under `-Werror`,
eight existing example rebuilds, and GCC 14 GNU17/strict C23 plus Clang
ASan/UBSan packet tests pass. Payload writes and physical DMA timing are not
simulated by this probe; actual raw-disc comparisons remain outstanding.
After updating the former all-raw-DMA rejection checks to odd raw counts,
the 90-check PIO probe passes in both modes. The prior BIOS read, convenience,
defaults, and G1 probes also pass all 151/128/14/4 checks after relinking.

The whole-range PIO pass expands `direct-raw-pio` to 304 checks, passing in
both Flycast modes. The production driver is exercised across 17/32/33-sector
boundaries with exact packet FAD/counts, buffer offsets and guards, odd raw
tails, short/oversized middle and final responses, and no command after an
error. A driver-local simulated clock verifies decreasing G1 lock budgets,
one shared absolute deadline, and partial-byte accounting when time expires
between commands. Size multiplication and pointer-wrap rejection precede I/O.
The full SH-4 GCC 16.2 build, driver and PIO probe under `-Werror`, nine existing
example rebuilds, and GCC 14 GNU17/strict C23 plus Clang ASan/UBSan packet
tests pass. These are simulated transport tests, not physical-drive evidence.
After relinking, the DMA, BIOS read, convenience, defaults, and G1 probes
also pass all 1426/151/128/14/4 checks in both modes.

The queued whole-range DMA pass adds `direct-dma-chain`: 320 checks pass in
both Flycast modes using the production direct driver and request engine,
with a simulated physical transfer and clock. Coverage includes cooked and
even-count raw chains, exact segment offsets/counts and payload guards,
tail-requeue fairness, partial failure, queued/active cancellation, a shared
deadline excluding initial queue residence, invalid continuations, and
metadata reclamation before callbacks. Admission/allocation failures issue
no physical command. No payload staging or implicit PIO/BIOS fallback is used.
The full SH-4 GCC 16.2 build, three driver units and the new probe under
`-Werror`, ten existing example rebuilds, and GCC 14 GNU17/strict C23 plus
Clang ASan/UBSan packet tests pass. After relinking, the PIO, bounded DMA,
BIOS read, convenience, defaults, and G1 probes pass 304/1424/151/128/14/4
checks in both modes. The bounded DMA probe now rejects 18-sector reads
only through the synchronous API, since queued reads support chaining.
These tests do not establish physical-drive payload, cache, IRQ, or timing
correctness; hardware validation remains outstanding.

The synchronous whole-range DMA pass expands `direct-raw-dma` to 3346 checks,
passing in both Flycast modes. Simulated MMIO verifies 17/18/32/33/34-sector
reads into cached/uncached RAM and PVR RAM, exact per-command FAD/count/buffer
and cache ranges, partial middle/final failures, G1 reacquisition failure,
and one absolute deadline with decreasing lock budgets. Complete destination,
pointer/size arithmetic, and FAD-span rejection precede I/O. GAPS retains its
bounded contract. The full SH-4 GCC 16.2 build, driver and DMA probe under
`-Werror`, ten other example rebuilds, and GCC 14 GNU17/strict C23 plus Clang
ASan/UBSan packet tests pass. Queued DMA, PIO, BIOS read, convenience, defaults,
and G1 probes still pass 320/304/151/128/14/4 checks in both modes. Payload
writes and real bus timing are not simulated by the MMIO probe; no physical
DMA/cache/IRQ or drive-support claim follows from these results.
