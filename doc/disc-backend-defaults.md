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
| `cdrom_read_sectors`, `cdrom_read_sectors_ex`, `cdrom_read_sectors_async` | Direct PIO/DMA with captured generic format | Corresponding `cdrom_bios_read_sectors*` APIs |
| `cdrom_change_datatype` | Generic direct-read format selection | `cdrom_bios_change_datatype` |
| `cdrom_reinit`, `cdrom_reinit_ex`, `cdrom_set_sector_size` | Direct post-boot reset/probe and format selection | Corresponding `cdrom_bios_*` APIs |
| `cdrom_get_subcode` | Direct SPI subcode query | `cdrom_bios_get_subcode` |
| `cdrom_cdda_play`, `cdrom_cdda_pause`, `cdrom_cdda_resume`, `cdrom_spin_down` | Direct SPI playback/drive control | Corresponding `cdrom_bios_*` APIs |
| Staged streaming | `cdrom_stream_session_start` and request/session lifecycle | `cdrom_bios_stream_session_start`, or legacy `cdrom_bios_stream_*` |

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

Raw subcode and playback controls also use direct SPI, independently of `/cd`,
with 10000 ms primary-command timeouts plus bounded recovery and `ERR_*`
results. Subcode output is only valid on success. Play retains repeat-count
saturation at 15 (infinite), but now rejects invalid modes/ranges with
`ERR_SYS`/`EINVAL`. The named BIOS play call retains the legacy invalid-mode
successful no-op and unbounded wait. Explicit BIOS typed CDDA status uses the
named BIOS subcode query; it does not cross into the direct driver.

## Sector formats and compatibility boundaries

The BIOS side of sector reads and format selection now has explicit names:
`cdrom_bios_read_sectors`, `cdrom_bios_read_sectors_ex`,
`cdrom_bios_read_sectors_async`, `cdrom_bios_change_datatype`,
`cdrom_bios_reinit`, `cdrom_bios_reinit_ex`, and `cdrom_bios_set_sector_size`.
Their generic counterparts now use direct transport. Boot setup, BIOS
filesystem/range paths, and BIOS read-reference examples use explicit functions
and retain their selected backend.

Generic format state is independent of the BIOS mode and explicit direct API
arguments. Supported cooked layouts are DATA_AREA/2048 with track type 1024
(Mode-1) or 2048 (Mode-2 Form-1); full raw is WHOLE_SECTOR/2352 with track type
0. DEFAULT part selects the corresponding layout, and size -1 means 2048.
Cooked track type -1 performs a direct readiness probe only during selection,
choosing Mode-2 Form-1 for CD_CDROM_XA and Mode-1 otherwise, preserving the
legacy selection rule. Raw track type -1 means any type without a probe.
Unsupported combinations fail with ERR_SYS/ENOTSUP before probing/resetting.
Failed selection preserves the old format. Successful boot-time BIOS setup
seeds the generic format once; before initialization it defaults to Mode-1.
After a media change, callers must select/reinitialize again.

Generic synchronous reads use one 10000 ms execution deadline. Generic async
reads capture the format at submission and require a nonzero timeout; unlike
the old BIOS alias, zero is now rejected with EINVAL. The explicit BIOS async
API retains zero-as-unlimited. Raw DMA requires even counts; deliberate PIO
selection supports odd counts. No read performs automatic format probing,
mode mutation, or transport fallback.

Generic reinitialization validates the format before the direct SPI reset and
uses the resulting readiness status for automatic selection, without a second
probe. A 10000 ms primary deadline plus bounded recovery applies. Media-state
errors are propagated even when reset transport succeeded. Format publication
occurs only on success, although failure may still have changed hardware state.
Callers must serialize reset/reinitialization against reads and streams on
both backends; G1 ownership does not make a multi-command read atomic.

BIOS sector-size bookkeeping now changes only after the firmware accepts a
mode update. Failed automatic track-type queries stop before submitting any
mode change. Callers must still serialize mode changes against outstanding
BIOS reads/streams, including requests waiting in the queue; command-level G1
ownership does not make global sector-mode changes safe for queued reads.

The direct path has an explicit per-call format contract for cooked/raw sectors.
Queued reads capture their format rather than depend on a later mode change.
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
or automatic backend switch.

`gdrom_direct_read_sectors_pio_async` now provides the same arbitrary-count
PIO read as an explicit queued choice, including odd RAW2352 counts and
two-byte-aligned buffers. Format and counts are copied at submission. Its
execution deadline excludes initial queue residence; cancellation follows
the polled transport's checks and bounded recovery. G1 releases between
commands, but PIO occupies the request worker for the whole operation rather
than requeueing like DMA chains. No payload staging or other transport is
used. Request progress counts copied bytes after each command attempt, while
the cumulative transport trace also includes excess bytes drained on error.
The result remains untouched on pre-execution cancellation or admission failure.

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
Odd raw DMA remains explicitly unsupported (`EINVAL`), with no implicit PIO
tail or extra-sector read. Applications needing odd raw counts can deliberately
select synchronous or queued PIO. Generic reads retain this DMA restriction.
Explicit BIOS APIs accept
configured raw layouts, which must not be silently reinterpreted as cooked.

## Legacy streaming migration (intentional API break)

The five old singleton `cdrom_stream_*` calls and `cdrom_stream_callback_t`
are removed, not retained as BIOS aliases. Existing programs must deliberately
choose BIOS by adding the `bios_` prefix, or migrate to direct sessions:

| Removed call | Direct-session replacement | Explicit legacy BIOS call |
| --- | --- | --- |
| `cdrom_stream_start` | `cdrom_stream_session_start`, then `cdrom_stream_session_wait_ready` | `cdrom_bios_stream_start` |
| `cdrom_stream_request` | `cdrom_stream_session_transfer_async` | `cdrom_bios_stream_request` |
| `cdrom_stream_progress` | `cdrom_request_get_status` / `cdrom_stream_session_get_status` | `cdrom_bios_stream_progress` |
| `cdrom_stream_set_callback` | Per-transfer `cdrom_request_callback_t` argument | `cdrom_bios_stream_set_callback` |
| `cdrom_stream_stop` | Cancel, wait, and destroy requests/session | `cdrom_bios_stream_stop` |

This is not a signature-only conversion. Direct sessions use explicit finite
sector counts (1..65535), cooked Mode-1/Mode-2 Form-1 formats, nonzero start/idle
timeouts, 32-byte-aligned DMA buffers and sizes, request objects, and callbacks
dispatched in thread context. They do not reproduce legacy PIO streaming,
firmware count sentinels, global callbacks, raw streaming, or progress conventions.
For direct PIO use `gdrom_direct_read_sectors` or
`gdrom_direct_read_sectors_pio_async`. An explicit BIOS stream still uses its
configured BIOS sector format; its callback type is now
`cdrom_bios_stream_callback_t`.

`examples/dreamcast/cdrom/stream` demonstrates the direct lifecycle;
`stream-bios` retains the explicitly named BIOS PIO/DMA demonstration. The
BIOS-selected ISO9660 streaming path and request-worker takeover helper use
the named BIOS calls, preserving their transport rather than crossing into
a direct session. No implicit compatibility macro or symbol hides this choice.

All ordinary convenience defaults covered above now use direct transport.
Raw `cdrom_exec_cmd[_timed]`, `cdrom_request_submit`, and `cdrom_abort_cmd`
remain low-level BIOS command-server interfaces: their command numbers and
parameter blocks are firmware contracts, not backend-neutral operations.

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

The explicit queued-PIO pass expands `direct-raw-pio` to 776 checks, passing
in both Flycast modes with production PIO transport logic and the real request
worker/callback lifecycle. It covers cooked/raw and odd-count reads, partial
and overflow progress, cancellation before execution and between commands,
initial-queue-time exclusion, shutdown rejection, and destination guards.
Custom executor progress metadata is now initialized at submission; previously
the shared progress helper clamped partial results to an empty segment. The
DMA-chain probe's 334 checks include synthetic custom-executor partial-error
and capacity-clamp cases through the production request layer. These cases
test accounting, not a physical bounded-DMA command.
The full SH-4 GCC 16.2 build, both affected driver units and probes under
`-Werror`, nine other example rebuilds, and GNU17/strict C23/Clang sanitizer
packet tests pass. The PIO API is present in the kernel and export stubs.
Existing DMA, BIOS read, convenience, defaults, and G1 tests still pass
3346/151/128/14/4 checks in both modes. Simulated registers and payloads do
not establish physical media, cache, IRQ, recovery, or throughput behavior.

The generic read/format/reinitialization routing pass adds `read-routing`:
247 checks pass in both Flycast modes. Production wrappers are checked with
transport/submission spies for supported and rejected layouts, captured format,
independent BIOS and filesystem selection, no per-read probing, failed-selection
preservation, reset/probe error-source mapping, direct-only PIO/DMA dispatch,
and nonzero async timeout policy. Explicit BIOS regression coverage remains
151 checks after replacing its historical generic-alias calls with named BIOS
calls. The 776/3346/334/128/14/4 PIO/DMA/queue/convenience/default/G1 regressions
also pass in both modes. The full SH-4 GCC 16.2 build, affected driver and
routing probes under `-Werror`, eleven other example rebuilds, and GNU17/strict
C23/Clang sanitizer packet tests pass. Boot-format seeding is compiled and
source-reviewed but not exercised by this no-CDROM-init routing probe. Actual
drive reset, live format detection/media changes, boot setup, and physical
transfers still require hardware validation.

The subcode/playback routing pass expands `convenience-routing` to 356 checks,
passing in both Flycast modes. It covers each direct control's timeout and
error/sense mapping, no BIOS fallback, repeat saturation, both play modes,
subcode selector forwarding, and named BIOS controls with direct `/cd` selected.
The 247/151/776/3346/334/14/4 read-routing/BIOS/PIO/DMA/queue/default/G1
regressions also pass in both modes. The full SH-4 GCC 16.2 build, `cdrom.c`
and ten disc probes/examples under `-Werror`, the basic CDDA example link,
and GCC GNU17/strict C23/Clang sanitizer packet tests pass. Named BIOS symbols
are generated in the export table and stubs. These routing spies do not
validate physical playback, subcode contents, drive spin-down, or recovery.

The final legacy-stream naming pass expands that probe to 405 checks in both
Flycast modes, including explicit BIOS PIO transfer/callback cleanup and DMA
submission-error unlock behavior under direct filesystem selection. Symbol
inspection confirms that the five removed singleton names are absent from
the rebuilt kernel and their BIOS-prefixed replacements are exported.
The full build, all three affected driver/filesystem units under `-Werror`,
and fifteen disc example/probe rebuilds pass. The new direct streaming
example and retained BIOS example are compile/link validated, not live-media
validated; the routing spies do not execute their end-to-end transfer paths.
