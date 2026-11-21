# libADX completion boundary

The fixed runtime baseline is Dreamcast ADX shipped in Sega Library 2.00J, not
later CRI products. Independent KOS-native behavior is the goal; matching a name
or adding a success-returning wrapper does not establish parity. The frozen
source inventory is [BASELINE_API.csv](BASELINE_API.csv). No percentage complete
is assigned: declarations differ greatly in complexity and share implementation.

The inventory contains 75 ADXT declarations (header version 5.93), 27 ADXF declarations (26 active and one disabled),
19 LSC, 12 active standalone SJ functions plus three disabled declarations,
and 12 SJ interface slots (three reserved, nine operations). It also records
three Sega MWPLY ADX facade functions and three ancillary file-selector helpers.
The ADX player manual is revision 5.78; header/manual version differences must
be reconciled, not silently treated as one identical specification. InitWce is a
Windows CE/DirectSound entry, not applicable to KOS; the three FSEL functions are
optional application file-picker UI from a separate library, outside runtime parity. The CSV is an
exhaustive declaration inventory of these named headers, not an assertion that
all SDK behavior or format combinations have already been reverse engineered.

## Runtime completion groups

Each entry in the API inventory maps to one row below. Present native facilities
are reusable foundations; remaining work is not closed merely by those facilities.

| ID | Capability | Existing native support | Remaining for baseline parity |
| --- | --- | --- | --- |
| H1 | Lifecycle and multi-handle management | player_init/start/stop plus explicit checked player/file collections, fair bounded polling, nonreused IDs and empty-only finish | The two-player retained-clock fixture fails with EPIPE during group poll. Audio-qualified multi-player output and device retirement/reuse remain open. No unsafe implicit reset; WinCE initialization is not applicable. |
| H2 | Playback sources | Immutable source/range/index loans, ACX/AFS memory/provider selection, owned filename source and ADX/container/sequence/joint bindings. Resident loans can bind finite zero-copy memory joints and expose queued input | Complete format conformance remains C1. The native facade uses explicit extents and ownership. |
| H3 | Metadata and framing queries | Validated metadata snapshot, strict header/endcode prefix probes and full-file adxcheck; truncation/malformed/unaligned/suffix tests | Admitted-format native queries implemented. CRI recognizer permissiveness and supplied-span endcode sizing are documented differences; complete baseline format conformance remains C1. |
| H4 | Embedded loop controls | Init-selected v3 repeat, playback-time release, and decoded-wrap query. Memory jobs release at the active trap. Sector-backed jobs apply release at transport EOS, retain prefetched data through the sample trap and floor raw loop offsets to the first compressed group | Stock seek and release ordering and one mounted-VFS loop/release of the shipped sector-aligned sample passed in both BIOS Flycast CPU modes. Physical GD-ROM latency and additional loop geometries remain open. |
| H5 | Volume and pan | Checked KOS linear 0..255 controls; signed pan/AUTO; opt-in stock 2.00J retained initial attenuation and live gain with signed cache and acknowledgements. One 44.1 kHz mono player passed live gain at 8,192 frames per ring in both Flycast CPU modes. | Live gain is disabled by default. The stereo composition remains held after EPIPE during polling after an acknowledged first request. Transition gain, fades and physical qualification remain open. |
| H6 | Playback clock and completion | Qualified source-output clock and signed offsets in sample, NTSC and PAL modes. Healthy concurrent titles independently reach logical PLAYEND. A failed output transfer preserves the compatibility status while native diagnostics report failure. | Sample, NTSC and PAL compatibility clocks freeze on the Flycast active-pause profile below. Device-audible end remains distinct from logical PLAYEND. |
| H7 | Service and input-buffer policy | Opt-in owner-driven service and direct title dispatch use one collection cadence latch. Phased ADX, WAVE, AIFF, AU and SPSD bindings preserve DECODER/RENDERER/FILE/STREAM/title/RELOAD order. Compatibility preparation uses admitted block geometry. Typed faults support checked STOP and caller-prepared REPLAY. Native fiber and group polling remain available. | Both Flycast CPU modes exercise live default stereo WAVE at NTSC and PAL with a 16,384-byte/channel sound buffer. Live default ADX REPLAY uses the same geometry. Other Audio compositions, exact physical storage timing and hardware output qualification remain open. |
| H8 | Error reporting and recovery | Independent diagnostic query/clear/owner notification; NOACT/STOP/bounded REPLAY with terminal input errors and opt-in running stall detection; monotonic delay and checked stop/replay preparation | Qualify target thresholds and output failure classification. Audio-qualified retirement and reuse remain open. |
| H9 | Start gate, pause and stop | Prefill, prepared-start gate, pre-output pause and checked software stop. An opt-in ordinary stereo title can stop and re-prime its own allocation with a new source. | The Flycast profile below supports five active pauses and resumes on one title. General active pause and repeatable retained restart remain open. A successful software stop is not permission to release sound RAM. |
| H10 | Native output controls | Ordinary routing, filter readback, exact initial pitch on supported profiles and ordinary mono live transpose. Opt-in ordinary stereo accepts live pitch through 88.2 kHz with the required ring, including a decoded but still playing tail. | The full stock rate range is not exposed by the public player. Fixed-phase retained live pitch and retained routing remain open. |
| H11 | Seamless queue and linked controls | Same-format PCM sequence or linked joint jobs. The linked player can start the loader with cached refill settings. A bound loader supports live registration, rotates at read completion, prefetches while PCM is full, and publishes pending-read count. Exact footer validation, optional two-span padded-header scan, history reset, live follow switch and final producer release | Audible transitions and physical drive timing. ReleaseSeamless is empty in the selected archive. Native loader repeat and finish are explicit. |
| H12 | Special processing | Unbound seek, SFA prefix, strict and batched paired live SFA with late final length and shared body/zero-group budget; producer PCM filter and peak analyzer selectable by the qualified source clock | Submitted stereo and decoded PCM discard are supported on the bounded Flycast profile below. General active discard and SFD demux integration to supply final length remain open. Zero groups preserve history. |
| F1 | File/partition services | Owned filename collections/loans and close-all; AFS directories/registries/overlays with exact provider-bound resolution; bounded sector requests and explicit flow policy | Transport/backend qualification for the accepted native filename/directory/lifecycle design. No hidden current-directory/reset; disabled GetFileRange is not a runtime obligation. |
| J1 | Stream joint | Caller-owned memory/ring/four-line queue byte joints: split, Get/Put/Unget/move/seed, query/error/reset/close; wrap region, SEPARATE/JOIN and node-pool counts; two simultaneous DATA loans with ordered return and request-fit queries for each joint kind | The observed two-loan usage is supported. Broader arbitrary SDK loan patterns and target behavior require conformance evidence. |
| L1 | Load/sequence controller | Bounded 16-entry loader with source IDs, copied labels, read-sector progress, pause and checked stop. The selected 2.00J flow policy captures the limit per source and admits batches from queued-DATA occupancy. File loans and a separate PCM sequence are available. | Exact cvFs completion visibility and physical drive/backend timing and retirement remain open. |
| M1 | Sega middleware facade | Native namespaced lifecycle, source, service, recovery and collection APIs; explicit mapping in NATIVE_MIDDLEWARE.md | Native facade disposition accepted on 2026-11-14. Runtime qualification is tracked in the underlying rows. |

### Active pause and discard: what remains

A paused title can discard eligible decoded frames before it has submitted
audio. Once playback has started, part of the unread stereo data is already
in sound RAM. Appending silence would play that data before pausing; dropping
it would resume at the wrong frame. Neither implements a pause.

The [Flycast active pause profile](README.md#flycast-active-pause) passed
five cycles on one finite 44.1 kHz stereo title in both BIOS CPU modes.
The title kept one handle and 128 KiB of sound RAM. Its sample and
centisecond clocks froze at the cut. Separate NTSC and PAL runs used
owner-delivered VBlank events: advancing the event counter while paused
did not advance title time. Resume set the clock from the running source
position; the next event added 100 ticks at a denominator of 5994 (NTSC)
or 5000 (PAL). These runs test clock arithmetic, not physical video timing.
After discards of 0, 128, 256, 0 and
128 frames, both channels resumed at the expected source frame. Retrying
a withheld cut result did not advance the cut or discard twice.

At a confirmed end of input, resume plays the remaining source frames and
pads the rest of the bank with silence. If discard removed the entire
tail, resume does not start the channels again. A temporary input shortage
instead returns `EAGAIN` without consuming frames or restarting playback.
Four cases in both BIOS CPU modes cover submitted and decoded tails, an
empty tail, and a retry after the producer supplies more input. With logical
PLAYEND enabled, completion follows the source boundary and checked stop.

The cut now counts frames from the current START, including completed
traversals of the 16,384-frame bank. A successful late cut must still fall
inside the committed stereo data whose source samples the player retains.
Both installed BIOS modes passed the public regression with uninterrupted
stereo source data between five late pauses. A separate backend-only
interpreter recording had seven extra silent blocks; its traced mixer
output passed, but the unmodified disk recording did not. A fresh
ordinary title after checked stop used a
separate allocation; these tests did not release or recycle the played
allocation. Hardware retirement is not established.

### Which player functions share a blocker?

| 2.00J entry | What the public tests distinguish | What still needs a native implementation or test |
| --- | --- | --- |
| `ADXT_Pause` | A confirmed stereo cut stops both voices. A timeout followed by retry returns the same cut. | Active mono playback and other stock nominal rates are outside the 44.1 kHz stereo profile. |
| `ADXT_DiscardSmpl` | Discard consumes the unread submitted head before decoded data and returns the common count actually removed. | Stock-valid titles that cannot yet pause also cannot discard that head. The native count deliberately excludes ring capacity when no frames moved. |
| `ADXT_GetStat` | Decoder end is not PLAYEND. On the checked ordinary mono profile, a live-transposed title now reaches PLAYEND after its final real frame and checked stop. | Status during active mono pause is not verified. A failed retained group poll does not, by itself, establish a status-getter bug. |
| `ADXT_GetTime` / `ADXT_GetTimeReal` | On the checked stereo cut, sample time, NTSC/PAL time and centiseconds freeze and resume. On checked ordinary mono, sample time and centiseconds track source position through live transpose; the terminal sample time in both Flycast modes was 81,920/44,100. | No active mono pause/resume clock is established. The mono witness does not extend the supported live pitch range or qualify other pause rates. |

The Dreamcast 2.00J filter parameter setter caches cutoff and resonance.
The selected Manatee adapter ignores its command selector, and the getter
reads the cached values. Native filter settings preserve this round trip.
The producer PCM filter callback is a separate operation.

## Codec and validation closure

Both installed BIOS Flycast CPU modes passed an ordinary 44.1 kHz stereo
ADX player at fixed volume through mounted ISO VFS. The synthetic v3 source
used an 8,192-frame sound ring per channel, a 32,768-frame decoded queue and
one embedded repeat followed by release. Both channels matched an independent
PCM reference through loop entry, wrap and the complete release tail. The
captures matched each other byte for byte across 131,072 frames. The producer
joined before file close, and checked software stop retained the sound
allocation. This qualifies that emulator playback profile. Retail disc and
audio timing and final-fetch retirement remain separate.

C1. Freeze the exact encoding/block/channel/version/header/history/loop/endcode
combinations accepted by the 2.00J decoder, then implement or document each
baseline rejection with tests. Current admission is encoding 3, 18-byte/4-bit,
mono/stereo v3/v5, zero extensions or the documented supported v3 loop layout
with 0..31 inserted leading frames. The strict path uses a block-aligned
encoded start. Memory jobs support independent logical sample positions and
whole-group byte offsets. Sector jobs accept a subsector raw start when its
sector floor points to the corresponding compressed group. Nonaligned
logical starts in this mode also need the logical interval to consume the
whole encoded loop span. In the stock trace, shorter intervals can leave
complete groups behind after fixed pad discard and drift on the next repeat.
Do not assume later ADX variants must be added simply because they exist.
See [FORMATS.md](FORMATS.md) for the distinction between active decoder behavior,
shipped corpus coverage and permissive parsing. Runtime WAV PCM16/ADPCM4 is a
documented middleware obligation. Native bounded PCM WAV8/16 mono/stereo reading
and seek plus native pipe/service/player bindings are implemented. CRI tag1 packed
WAV4 now uses software AICA reconstruction through those bindings, with bounded
history-rebuilding seek. Hardware PCM equivalence remains open. SPSD PCM16 has
reader/pipe/service/player support for contiguous mono and exact-length
sample-interleaved stereo with per-channel length. The selected SDK decoder
supplies the frame-count and copy rules. Shipped stereo corpus evidence and
other SPSD layouts remain open.
AIFF signed8/BE16 mono/stereo has a bounded reader, metadata, seek and filters;
AIFF pipe/service/player composition is implemented.
The AU reader now handles mono/stereo signedPCM8/signedPCM16/mu-law8 with exact
conversion, bounded input, seek, filters and native pipe/service/player bindings.
Remaining container conformance is open.

C2. Close native arithmetic conformance: predictor operation order, rounding,
clipping, initial/carried histories and coefficient setup against the baseline.
Version 3 and 5 setup through 44100 Hz selects Q12 pairs from the rounded
Dreamcast model. A linked SDK instruction run in the SuperH p-code emulator
matches the sampled coefficient witnesses. The 13-bit scale and the scalar
SH4ZAM/FMAC predictor match the SDK's one-group PCM and history on the tested
mono/stereo witnesses in both BIOS Flycast CPU modes, including FTRC saturation.
SDK batching retains float history across the groups of a decode job. Its
input byte consumption can differ from the reported PCM frame count at a trap.
The opt-in joint reader and player service schedule those jobs on memory,
mirrored-ring and one-head JOIN/SEPARATE queue input. They publish separate
byte and PCM counts and quantize the predictor at the end of each job.
Synthetic memory trap, release, and output
wrap witnesses match stock output and positions. The normal reader keeps
fixed group boundaries for fragmentation-independent decoding. Sector-backed
loop fixtures exercise the provider seek, residual pad discard and release
with prefetched DATA. Paired live SFA jobs keep late-length, footer and
sealed-input validation. Retail arithmetic remains open.

V1. For every runtime row: named return/state/error/control tests, supported
format combinations and numerical/control units; full host/sanitizer and staged
SH-4 link evidence. No unresolved behavior may be marked implemented by inference.

V2. Qualify music output, starvation/recovery, loop seams, transitions, pause,
stop/reuse and multi-player resource behavior on the accepted Audio backend.
Emulator output and real-hardware results remain separate. A software join is not
sample-fetch retirement; the current player deliberately retains sound allocation.

V3. Complete the stock-KOS adapter milestone after the fork implementation, as
requested by the owner. This does not block a clearly scoped fork-only release,
but remains part of the overall KOSdec deliverable.

V4. Integrate the exact reviewed source/dependencies into master, rebuild installed
headers/archive and dependent consumers, and record the commit. A private worker,
staged archive or review handoff is not completion of this requirement.

## Overall project boundary

The existing native path already supplies standard ADX decoding, buffered file
input, loop metadata, whole-file sequence repeat and fixed-rate output. The
file_audio consumer and file_player tests exercise that composition. Application
integration is separate from middleware completion. Source-end counters are not
an audible clock. Runtime live stereo rate changes and sound retirement
remain Audio/Integration work; no alternate firmware behavior is hidden here.

The separate host-tool/authoring milestone remains open: compatibility checking
exists, while encoding/conversion and archive authoring do not. Sega's ADX record
facade is not a decoder operation; assess it under authoring rather than silently
expanding playback work. SFD, AHX and newer CRI codecs are outside this libADX
runtime completion boundary.

The definitive finish condition is closure of C1/C2, each applicable runtime row,
and V1/V2/V4 with recorded evidence and no hidden unimplemented paths. V3 and host
authoring are separately visible project milestones. This is a finite backlog,
not a claim that all remaining entries are small or already specified completely.

## Resolved applicability

`ADXT_InitWce` takes a DirectSound object and is explicitly documented for Windows
CE in the shared header. Neither a definition nor a reference appears in the two
Dreamcast ADX ELF archives; ordinary `ADXT_Init` is present. KOS requires its native
lifecycle and playback services, not a Windows CE initializer or success stub.

The three `FSEL_*` declarations belong to a separate file-selector library. Their
interface includes drawing, color/font metrics, VBlank and peripheral selection;
the ADX ELF archives do not reference them. Applications supply selected filenames
or byte ranges to libADX. Rebuilding the file-picker UI is outside runtime parity.

The three SJUNI Get/Put/Unget callback registration declarations are disabled in
the 2.00J header and absent from the Dreamcast ADX/Sofdec libraries. The error
callback and request-fit vtable operations are active.
The remaining lifecycle and output qualifications are tracked in the matrix above.

The native middleware batch is described in [NATIVE_MIDDLEWARE.md](NATIVE_MIDDLEWARE.md).
It materially supplies the source/control/lifecycle/recovery/link/framing paths;
it does not by itself close the remaining Audio, conformance or applicability
gates. Integration accepted its explicit contexts/native paths, event-driven
service and namespaced facade design on 2026-11-14. The design-disposition items
in H1/H7/F1/L1/M1 are resolved by that acceptance; their runtime, timing, backend
and physical-output qualifications remain open. This is not full MWPLY ABI or
SDK scheduling equivalence.
