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
| H1 | Lifecycle and multi-handle management | player_init/start/stop plus explicit checked player/file collections, fair bounded polling, nonreused IDs and empty-only finish | Native global-reset replacement disposition; actual multi-player output and Audio-qualified device retirement/reuse. No unsafe implicit reset; WinCE initialization is not applicable. |
| H2 | Playback sources | Common immutable source/range/index loan; ACX/AFS memory/provider selection; owned filename source; existing ADX/container/sequence/joint bindings | Native admission/control composition supplied; complete format conformance remains C1. CRI ABI is not implemented. |
| H3 | Metadata and framing queries | Validated metadata snapshot, strict header/endcode prefix probes and full-file adxcheck; truncation/malformed/unaligned/suffix tests | Admitted-format native queries implemented. CRI recognizer permissiveness and supplied-span endcode sizing are documented differences; complete baseline format conformance remains C1. |
| H4 | Embedded loop controls | Init-selected v3 repeat, playback-time release, source wraps and atomic decoded-loop query/player snapshot | Release-only while playing matches the baseline manual. Full metadata/history coverage remains C1; exact SDK prefetch/trap batching timing is unqualified. Counter is decode-side, not audible-time. |
| H5 | Volume and pan | Checked KOS linear 0..255 controls; signed per-channel pan/AUTO and getter, persistent across bindings | Exact attenuation units/defaults/getters and downstream quantization; linear8 cannot represent the full logarithmic range. Pan uses strict argument rejection and immediate AUTO resolution; native/hardware qualification remains. |
| H6 | Playback clock and completion | Software source/copy counters and source-end state | Qualified output/source clock, audible end, offsets and time modes; source EOF is not PLAYEND. Audio dependency. |
| H7 | Service and input-buffer policy | Bounded producer service/private fiber, fair group polling, loader sector/time/default refill and safety queries | Integration acceptance of event-driven native equivalence instead of SDK global server/frequency semantics; target scheduling/performance qualification. Input estimates are compressed bytes, not audible time. |
| H8 | Error reporting and recovery | Independent diagnostic query/clear/owner notification; NOACT/STOP/bounded REPLAY controller with monotonic delay, checked stop retries and explicit replay preparation | Running-stall detection, exact SDK threshold/numeric disposition and Audio-qualified resource reuse. Replay only on terminal input IO_ERROR; no implicit sound reclamation. |
| H9 | Start gate, pause and stop | Prefill, explicit prepared-start gate/readiness, producer pause and checked software stop | Audible pause/resume and reusable checked stop. Audio dependency; gate is software source admission, not audible pause. |
| H10 | Native output controls | Requested output Hz only; checked update | Direct/effect levels and routing, filter, transpose/detune units/getters; stereo-coherent live updates. Audio dependency. |
| H11 | Seamless queue and linked controls | Same-format PCM sequence and repeat/release; atomic queued-entry query; opt-in adjacent compressed-record playback and producer link release | Qualified audible transitions, SDK prefetch/queue timing and linked-format conformance. ReleaseSeamless is empty in the selected archive; native repeat release is explicit. |
| H12 | Special processing | Unbound seek, SFA prefix, finite live ring framing with shared body/zero-group length budget; producer PCM filter and peak analyzer | Unknown-length SFD and live length-revision contract; Audio-qualified sample discard and playback-clock integration for amplitude. Zero groups preserve history. |
| F1 | File/partition services | Owned filename collections/loans and close-all; AFS directories/registries/overlays with exact provider-bound resolution; bounded sector requests and explicit flow policy | Native filename/directory/global-lifecycle equivalence disposition and transport/backend qualification. No hidden current-directory/reset; disabled GetFileRange is not a runtime obligation. |
| J1 | Stream joint | Caller-owned memory/ring/four-line queue byte joints: split, Get/Put/Unget/move/seed, query/error/reset/close; wrap region, SEPARATE/JOIN and node-pool counts | Forward-only live decoding, sealed EOF and producer-owned player binding supplied; broader multi-borrow baseline conformance remains. Native one-borrow-per-line/reserved-node admission and typed identity/status are deliberate differences; no CRI ABI claim. |
| L1 | Load/sequence controller | Bounded16-entry loader with source IDs/copied labels/status/read sectors/pause/refill and checked stop; filename/source-loan composition; PCM sequence | Exact SDK refill/service disposition and backend retirement qualification; labels are not paths and byte concatenation is not seamless PCM playback. |
| M1 | Sega middleware facade | Native namespaced lifecycle, source, service, recovery and collection APIs; explicit mapping in NATIVE_MIDDLEWARE.md | Integration disposition of native equivalents versus MWPLY ABI. No facade internals inferred from declarations; no MWPLY vtable supplied. |

## Codec and validation closure

C1. Freeze the exact encoding/block/channel/version/header/history/loop/endcode
combinations accepted by the 2.00J decoder, then implement or document each
baseline rejection with tests. Current admission is encoding 3, 18-byte/4-bit,
mono/stereo v3/v5, zero extensions or the documented supported v3 loop layout
with 0..31 inserted leading frames and a block-aligned encoded start. Arbitrary
requested loop starts are aligned by the encoder; raw nonaligned encoded starts
remain unsupported, pending a distinct baseline runtime contract.
Do not assume later ADX variants must be added simply because they exist.
See [FORMATS.md](FORMATS.md) for the distinction between active decoder behavior,
shipped corpus coverage and permissive parsing. Runtime WAV PCM16/ADPCM4 is a
documented middleware obligation. Native bounded PCM WAV8/16 mono/stereo reading
and seek plus native pipe/service/player bindings are implemented. CRI tag1 packed
WAV4 now uses software AICA reconstruction through those bindings, with bounded
history-rebuilding seek. Hardware PCM equivalence remains open. SPSD monoPCM16 contiguous payloads now
have reader/pipe/service/player support. Other SPSD layouts and encodings remain
unsupported: the baseline forces16-bit copying and has a stereo-layout contradiction
with common SPSD. Close that named contract/corpus gap before expanding admission.
AIFF signed8/BE16 mono/stereo has a bounded reader, metadata, seek and filters;
AIFF pipe/service/player composition is implemented.
The AU reader now handles mono/stereo signedPCM8/signedPCM16/mu-law8 with exact
conversion, bounded input, seek, filters and native pipe/service/player bindings.
Remaining container conformance is open.

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

## Resolved applicability

`ADXT_InitWce` takes a DirectSound object and is explicitly documented for Windows
CE in the shared header. Neither a definition nor a reference appears in the two
Dreamcast ADX ELF archives; ordinary `ADXT_Init` is present. KOS requires its native
lifecycle and playback services, not a Windows CE initializer or success stub.

The three `FSEL_*` declarations belong to a separate file-selector library. Their
interface includes drawing, color/font metrics, VBlank and peripheral selection;
the ADX ELF archives do not reference them. Applications supply selected filenames
or byte ranges to libADX. Rebuilding the file-picker UI is outside runtime parity.
These dispositions do not close H1 lifecycle, M1 facade, host authoring, or any
other remaining runtime behavior.

The native middleware batch is described in [NATIVE_MIDDLEWARE.md](NATIVE_MIDDLEWARE.md).
It materially supplies the source/control/lifecycle/recovery/link/framing paths;
it does not by itself close the remaining Audio, conformance or applicability
gates. Integration accepted its explicit contexts/native paths, event-driven
service and namespaced facade design on 2026-11-14. The design-disposition items
in H1/H7/F1/L1/M1 are resolved by that acceptance; their runtime, timing, backend
and physical-output qualifications remain open. This is not full MWPLY ABI or
SDK scheduling equivalence.
