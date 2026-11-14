# KOSdec media addon

KOSdec owns `addons/libkosdec`: independent `libADX`, `libSFD`, `libAHX`
components and host tools. Full Dreamcast-era ADX middleware parity is the first
milestone, targeting KOS Experimental before stock KOS. Full SFD and AHX development follows ADX. The original bounded SFD video-only
prototype is preserved under `addons/libkosdec/libSFD`; its explicit archive,
standalone OIX/AMX example and host fixture tests are opt-in. The example owns
startup cache/MMU state; the archive does not. Preserve the documented profile,
float math, dependency provenance and separate hardware/runtime gates.
The owner's full-scope requirement is tracked in `addons/libkosdec/libADX/PARITY.md`;
asset-specific priorities do not narrow that objective. Keep unknown and incomplete
capabilities explicit, including the outstanding function-by-function baseline map.

The optional host `tools/adxcheck` validates standalone assets with the unchanged
decoder's explicit host math model. Its declared fields are not admitted metadata;
compatibility requires the full body/footer and physical file EOF. Keep it read-only,
bounded in memory and separate from target playback/SH-4 equivalence claims.
Run the addon `tools-test` and `tools-sanitize` targets for checker changes.

The current libADX archive is a bounded float decoder, memory/stream PCM readers and
a synchronous KOS file adapter, asynchronous direct-disc source and bounded ACX/AFS
member lookup, plus a bounded 16-entry PCM sequence queue and a caller-owned SPSC PCM pipe. A separate native fiber/disc exerciser demonstrates cooperative
scheduling. An optional native audio player uses existing fork stream APIs and a private
fiber executor, with caller-owned buffers and source. It supports one output start,
source prefill, poll, live volume/pan and checked software stop/join, retaining
sound allocations, same-format source release/rebind or early cancellation,
bounded source seeking and checked output-frequency changes. Its README and public header define the limits. Do not claim
full middleware parity, audible completion or stopped sound-allocation reuse. Keep individually linkable decoder and
service layers; no success-returning stubs for unimplemented parity capabilities.

Use the root source-evidence gate before changing format/math or SDK behavior.
Preserve exact float ordering, history boundaries, conversion and FPU-state
contracts. Integer syntax/counts and final PCM representation are intentional.
Retain dependency provenance and never copy proprietary implementations/assets.

Interface partners: Audio owns output ports, firmware, sound RAM and playback
lifetime; Storage owns disc/filesystem service; Fibers owns provider/carrier
semantics; Kernel owns global MMU/cache/SQ policy. Shared interface changes need
their coordination. Codec-only work does not authorize changes to those systems.

In the assigned isolated checkout, run the addon `test`/`sanitize` targets, build
the strict SH-4 archive, and validate staged header/archive consumption. Numerical
oracle tests, target links, emulator playback and physical-hardware measurements
are distinct gates. Record exact evidence and remaining gaps in the private
handoff. Do not build in canonical master or commit/publish without authorization.

Direct-disc input owns a request and staging until successful request destruction.
Decoder cancellation is not transport retirement. Call disc_close until DONE
before reclaiming buffers/ranges/mappings, including after errors. Preserve the
exclusive fiber-provider rule; only yield outside driver locks and FPU mode changes.

Sequence transitions reset predictor history and validate each footer before
advancing. Queue counters describe decoded PCM, not input read-ahead or playback.
No audible seamless/CRI ABI claim follows from passing concatenation tests.

PCM pipe ownership is one producer and one consumer. Source controls are
producer-only; cancel requests require a producer acknowledgement and wakeup.
Terminal publication and software drain do not join users or retire providers.
Keep ring/output memory separate from DMA staging and preserve immutable format
publication. No scheduler or device dependencies belong in the pipe object.

Audio is explicitly in scope per owner clarification. The standalone audio
example leaves shared sound code unchanged, uses exact splitting to avoid the
existing aligned PCM16 ASM permutation, and retains sound allocations after
software stop. Runtime/source-copy success is not final-fetch or natural-end
qualification. Audio owns the underlying splitter correction and retirement.

The producer control service wraps the pipe with one copied command/receipt slot.
Only its producer step mutates source state; one controller posts/takes receipts.
Use existing fork fiber scheduling and latched wakes, never poll source controls
from an audio callback. Closure resolves accepted commands racing EOF/cancel;
acknowledgement still is not producer return or provider retirement. Production
pause leaves queued PCM readable and is distinct from audible playback pause.

Player calls are serialized on one ordinary owner thread. Natural release at
SOURCE_END or explicit early cancellation disables PCM admission before joining; IDLE/prefill callbacks
supply silence, and rebind checks the original format before admitting PCM.
Provider retirement remains separate; no sound alloc/start/stop occurs on rebind. The producer fiber
never calls sound APIs. Detach callback/userdata on final stop, or disable PCM/wakes before source join;
keep first failure distinct from stop/retry errors. On successful stop retain the
sound allocation/handle while providers retire separately. No player API may
reset global audio or reinterpret source drain as hardware completion. New SDK
attenuation/pan parity must distinguish CRI's units from current KOS0..255 controls.

Source seek is serialized while unbound: decode from zero to rebuild full-group
history, with at most one provider call/32 discarded frames per step. The pipe
advances pending seeks on its producer, never emitting skipped PCM. Loop-repeat
seek is excluded; no audible-time or constant-time seek claim. Source and output
rates are separate: rebind compares source format, while checked playback-rate
updates persist on the running output. No CRI semitone/fade mapping is implied.
Partial pipe space can accept min(free,32) frames; never require a whole group
of free ring slots when cached partial groups must finish prefill.

Coefficient admission covers integer rates 1..48000 Hz and cutoffs0..floor(rate/2)
using original certified quantization boundaries; above44.1k is an extension beyond
baseline ADX Talk. Keep the selector as the single codec rate/cutoff authority.
Metadata is valid only after header_ready. Preserve the original float/FMAC
predictor and regenerate/check setup certificates and independent PCM oracles when
changing this domain. Native player effective output remains8000..48000Hz; lower
codec rates need an explicit in-range override. Admission is not playback or
hardware certification. See libADX/COEFFICIENTS.md for the proof and table cost.

An optional post-decode PCM filter belongs to the serialized source producer.
Configure before binding or after join; keep callback/context alive through all
producer calls. It may modify only its channel's emitted interleaved PCM span,
never retain it, reenter the source, block/yield or change FPU control modes.
History remains unfiltered; loop padding and seek-discarded PCM do not invoke it.
Sequence filters persist across entries; standalone decoder initialization clears
configuration, while stream seek preserves it. Consumer/audio callbacks never run
these filters. Include callback cost in the producer budget. This facility does
not establish playback-time amplitude metering or a qualified audible clock.

Byte joints are serialized compressed-input storage, not a concurrent PCM pipe
or DMA transport. Memory never offers FREE bytes. Ring mirror publication covers
only committed bytes; do not replace it with whole-prefix copies. Keep one borrow
per line, token/bounds checks, prefix Put/suffix Unget, and reset/close BUSY gates.
External providers must be joined separately. Native kind/status and ownership
restrictions do not imply CRI ABI or full SJ equivalence; see the completion map.

Generic byte queues have four lines and caller-owned node workspace. Separate
mode returns whole heads; Join permits splitting/adjacent merging within one
backing allocation. Reserve one node per borrow; full rollback must remain
possible despite pool exhaustion. Partial operations can return LIMIT unchanged.
Seed rejects overlap; reset drops queued references without freeing buffers.
Do not treat an empty queue as EOF or native node counts as proprietary ABI.

Joint readers use the existing strict incremental float decoder; empty input is
not EOF until explicit joint_finish seals DATA admission. Hold whole chunks until
consumed so decode progress cannot precede a failed partial-return allocation.
Read work stays one decoder step; queue whole-head retention can add backpressure.
Cancel retains input until close; close discards unread admitted bytes and never
joins/reset/frees the producer or joint. Queue detach explicitly removes a prefix
from ownership accounting without freeing bytes. Player binding uses explicit joint constructors.

A bound joint feed runs only on the PCM producer, at most once per read. Paused,
cancelled, full-pipe and consumer paths never feed. External transport callbacks
stage/wake; they cannot mutate the live joint. END uses checked sealing, WAIT
permits buffered DATA, ERROR closes source production. Existing player fiber owns
reader/joint until release/cancel/stop joins; reader close and provider retirement
remain separate. Keep same-format rebind checks and retained Audio allocation.
Forward-only joint sources reject loop/repeat/enqueue controls; no hidden seek.

The baseline loop flag is selected before start and can only be released during
playback. Do not invent live re-enable to close H4. Embedded-loop counts are
source rewinds, not audible time; SDK updates its count in a decoder trap. Publish
low32 bits from the producer with the existing lock-free atomic discipline;
controller queries must not read the producer-owned64-bit wraps directly.
Sequence cycles and forward-only joint input are not embedded-loop counts.

Amplitude records measure complete PCM windows; peak32768 is valid for INT16_MIN.
Manual feed backpressures; the optional filter adapter latches overflow instead
of silently dropping records or altering playback PCM. Keep one producer and one
consumer per channel, release/acquire publication, explicit source-frame selection,
and storage lifetime through producer join. Do not call analysis timestamps an
Audio-qualified clock or replace the user's existing filter without composition.
Native typed work sizing differs from the SDK; no peak-loop FPU/SH4ZAM claim.

Compressed silence insertion means complete zero residual ADX groups, not clearing
predictor history. Preserve ring wrap/mirror publication and producer ownership;
caller maintains header sample length and group alignment. Output sample discard
belongs to the Audio queue/time contract, not a decoder seek or pipe-read shortcut.

Resident partition metadata is published only after the whole AFS table passes
bounds. Loading is one read_at call<=512 bytes/64 entries per step, with explicit
workspace sizing and sticky failure/cancel. Registry references prevent metadata
close while bound; all operations are caller-serialized. Copied member views bind
an explicit immutable archive provider and may outlive metadata, never provider
retirement. Sparse overlay push/pop uses caller-owned links; newest count bounds
IDs and zero offsets fall back through older layers. Preserve selected archive
identity, reference counts and same-base idempotence; replacement/unbind/close
detach all associated links. Native byte-range work sizing is not CRI's compact
sector ABI. Rebuild consumers after registry layout changes.

Sector request state owns only its callback/output borrows, not backend staging.
Each step performs one <=512-byte read or zero fill; WAIT must be rescheduled.
Cursor commits completed sectors on end/error, remains at request start on stop.
Final-sector padding must never read beyond the selected extent. Keep exact byte,
output and sector progress separate. Close/seek/stop do not retire the provider's
independent DMA/request state. No VSync/audio IRQ callbacks or global handle shim.

Joint-destination requests can retain FREE across partial reads/WAIT. DATA consumers
may run serially; no second producer/reset/close/seal while active. SEPARATE heads
publish only after filling; final tail splits may need spare nodes. NEED_OUTPUT
retains ownership and must not spin. Stop returns unpublished storage but never
removes prior DATA. Exact final byte ranges avoid injecting decoder suffix bytes.
Check successful stop/close before recycling held storage; rebuild request state.

PCM WAV uses a distinct bounded reader, not widened ADX encoding admission.
Preserve RIFF chunk/padding bounds and byte-safe PCM16 loads; U8 conversion is
exact integer format conversion. PCM8/16 and CRI tag1 packed4 use existing
pipe/service/player bindings. Packed4 reconstructs AICA-model PCM16, preserving
per-channel history and low-first nibbles from channel-interleaved bytes. Do not
substitute generic Yamaha rounding or add a high-pass. Preserve whole four-frame
groups, checked frame counts and bounded32-frame seek rescans without filters.
This does not establish hardware equivalence or broader SPSD support.
Seek/filter mutation/reinit only after users are unbound and joined. Preserve
producer-only I/O, source format publication and existing retained Audio resources.

AU is a separate bounded PCM/mu-law reader. Preserve signed8-bit versus WAV
unsigned8-bit semantics, big-endian byte-safe16-bit loads, and exact G.711
expansion. Source extent bounds unknown-size payloads; skip annotations by offset.
Seek/filter changes are unbound producer operations; no provider retirement or
Audio claim. AU uses the same pipe/service/player producer binding and retained-output rules
as WAV. Rebuild pipe/service/player consumers for added source state.

AIFF reading validates FORM/COMM/SSND bounds, either chunk order, frame products
and full80-bit integral sample rates. Signed8/BE16 conversion is exact and
byte-safe; no FPU-mode changes. Ignore alignment padding after declared frames,
not missing frame bytes. INST/MARK loops and AIFF-C are unsupported. The reader
uses the same producer/filter/seek ownership and pipe/service/player bindings.
Rebuild consumers after source-state layout changes; Audio lifetime is unchanged.

SPSD currently admits only the verified contiguous monoPCM16 intersection. Keep
strict64-byte version, codec/channel/index, rate and extent checks. The SDK's
unconditional width override and stereo-layout contradiction do not establish
compressed or common block-interleaved support. Do not widen admission without
closing the named source/corpus gap. Standard producer/filter/seek and retained
output contracts apply; loop/config fields are ignored, not implemented.

Prepared-start gating belongs to the player owner: producer buffering continues,
source PCM admission waits, and retained output still polls silence. Readiness
requires validated format/prefill, resets at source boundaries and is not audible
completion. Keep prefill timeout separate from intentional ready waiting. Gate
changes after admission apply to future sources; audible pause remains Audio.

The loading controller owns one bounded request and pending borrowed providers,
not file descriptors. Keep16-entry/name/ID bounds, exact last-sector bytes and
explicit final joint sealing. Active-sector completion is separate from new
low-water admission. Pause retains a partial borrow; failed stop cannot release
storage. Published DATA survives stop and source metadata completion is not DMA
retirement. Byte loading is not PCM sequence joining or audible seam proof.

Signed player pan uses per-channel requested -15..15/AUTO settings, resolved at
source admission or live update. Preserve independent AUTO across bindings and
legacy set_output bytes; its getter reports equivalent signed positions after a
legacy call. Checked update failure retains the previous requested snapshot but
may partially affect hardware, so FAILED/stop is mandatory. Discrete pan encoding
needs no FPU transform. Full-range logarithmic volume requires a separate reviewed
Audio interface; linear8 approximation does not close attenuation parity.
