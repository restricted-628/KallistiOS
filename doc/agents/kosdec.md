# KOSdec media addon

KOSdec owns `addons/libkosdec`: independent `libADX`, `libSFD`, `libAHX`
components and host tools. Full Dreamcast-era ADX middleware parity is the first
milestone, targeting KOS Experimental before stock KOS. SFD and AHX are deferred.
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
