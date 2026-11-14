# libADX completion boundary

The fixed runtime baseline is Dreamcast ADX shipped in Sega Library 2.00J, not
later CRI products. Independent KOS-native behavior is the goal; matching a name
or adding a success-returning wrapper does not establish parity. The frozen
source inventory is [BASELINE_API.csv](BASELINE_API.csv). No percentage complete
is assigned: declarations differ greatly in complexity and share implementation.

The inventory contains 75 ADXT declarations (header version 5.93), 27 ADXF,
19 LSC, 12 active standalone SJ functions plus three disabled declarations,
and 12 SJ interface slots (three reserved, nine operations). It also records
three Sega MWPLY ADX facade functions and three ancillary file-selector helpers.
The ADX player manual is revision 5.78; header/manual version differences must
be reconciled, not silently treated as one identical specification. InitWce and
ancillary helpers require explicit applicability disposition. The CSV is an
exhaustive declaration inventory of these named headers, not an assertion that
all SDK behavior or format combinations have already been reverse engineered.

## Runtime completion groups

Each entry in the API inventory maps to one row below. Present native facilities
are reusable foundations; remaining work is not closed merely by those facilities.

| ID | Capability | Existing native support | Remaining for baseline parity |
| --- | --- | --- | --- |
| H1 | Lifecycle and multi-handle management | player_init/start/stop; caller-owned state | Global handle/reset/close semantics and multi-handle conformance; Audio-qualified device retirement/reuse; InitWce needs Dreamcast applicability disposition. |
| H2 | Playback sources | file/stream/memory, ACX/AFS views, sequence | Unified source admission/control equivalents, memory-index semantics and SJ source behavior; direct filename BGM composition supplied, not CRI ABI compatibility. |
| H3 | Metadata and framing queries | Validated metadata snapshot, strict header/endcode prefix probes and full-file adxcheck; truncation/malformed/unaligned/suffix tests | Admitted-format native queries implemented. CRI recognizer permissiveness and supplied-span endcode sizing are documented differences; complete baseline format conformance remains C1. |
| H4 | Embedded loop controls | v3 loop repeat/release and counters | Baseline loop on/off/count semantics and every baseline-accepted metadata/history layout; current release-only source control is narrower. |
| H5 | Volume and pan | Checked KOS linear 0..255 controls | CRI attenuation/pan units, defaults, getters and edge cases; current KOS control units differ. |
| H6 | Playback clock and completion | Software source/copy counters and source-end state | Qualified output/source clock, audible end, offsets and time modes; source EOF is not PLAYEND. Audio dependency. |
| H7 | Service and input-buffer policy | Bounded producer service, private fiber, input cache and PCM pipe | Per-handle/global service and reload/safety units/policy; buffer-time queries and input-SJ equivalence. |
| H8 | Error reporting and recovery | Sticky codec status, I/O errno, preserved player/stop errors | Baseline recovery modes, callbacks, error-code mapping/reset and failure-transition conformance. |
| H9 | Start gate, pause and stop | Prefill plus producer pause and checked software stop | Explicit prepared-but-not-playing gate; audible pause/resume and reusable checked stop. Audio dependency. |
| H10 | Native output controls | Requested output Hz only; checked update | Direct/effect levels and routing, filter, transpose/detune units/getters; stereo-coherent live updates. Audio dependency. |
| H11 | Seamless queue and linked controls | Bounded same-format PCM sequence and repeat/release | Baseline queue timing, linked controls and qualified audible seamless transitions; PCM concatenation alone does not establish this. |
| H12 | Special processing | Unbound decode-from-start seek; caller-buffer SFA header; producer-side PCM filter | Sample-discard contract, amplitude analysis/work sizing, live SJ/SFD header insertion and silence injection. |
| F1 | File/partition services | VFS/direct-disc sources, bounded AFS member views | Partition registration/loading/status, directory/name/range services, sector-read/seek/count/reload/error contracts and lifecycle equivalence. |
| J1 | Stream joint | Caller-owned memory/ring/four-line queue byte joints: split, Get/Put/Unget/move/seed, query/error/reset/close; wrap region, SEPARATE/JOIN and node-pool counts | Live input integration and multi-borrow baseline conformance. Native one-borrow-per-line/reserved-node admission and typed identity/status are deliberate differences; no CRI ABI claim. |
| L1 | Load/sequence controller | Bounded same-format PCM sequence | Filename/range queue, per-stream IDs/names/status/read sectors, flow-limit and service/error/pause/stop equivalence. |
| M1 | Sega middleware facade | Native KOSdec API | Decide and document native equivalent versus MWPLY-facing integration; no facade supplied. |

## Codec and validation closure

C1. Freeze the exact encoding/block/channel/version/header/history/loop/endcode
combinations accepted by the 2.00J decoder, then implement or document each
baseline rejection with tests. Current admission is encoding 3, 18-byte/4-bit,
mono/stereo v3/v5, zero extensions or the documented supported v3 loop layout
with 0..31 inserted leading frames and a block-aligned encoded start. Arbitrary
requested loop starts are aligned by the encoder; raw nonaligned encoded starts
remain unsupported, pending a distinct baseline runtime contract.
Do not assume later ADX variants must be added simply because they exist.

C2. Close native arithmetic conformance: predictor operation order, rounding,
clipping, initial/carried histories and coefficient setup against the baseline.
The certified general coefficient domain and independent host vectors are strong
existing evidence; they do not establish every SDK rounding decision or retail
hardware result. SH4ZAM/FPU policy must retain exact arithmetic rather than
substitute an unrelated vector transform.

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
