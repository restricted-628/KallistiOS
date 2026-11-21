# Native middleware ownership and control

These interfaces implement original KOS behavior against the fixed Dreamcast
baseline. They are not CRI/MWPLY ABI replacements. Rebuild consumers: source,
service and player state layouts have changed. Runtime completion remains tracked
in COMPLETION.md. Integration accepted the explicit-context, native scheduling
and facade dispositions below on 2026-11-14; this is not full runtime parity.

## Sources and filenames

`adx_source.h` selects an immutable memory/provider extent, byte range, ACX member
or AFS member. Indexed selection is incremental and transactional: a failed or
cancelled lookup retains the previous selection. One checked descriptor loan can
feed the existing stream, sequence, container, loader and sector-request APIs.
The loan covers every copied descriptor consumer, not just its original storage.
Return it only after all consumers join and their provider obligations retire.

`ADXT_StartMem2` maps to an explicit resident extent. The native
`ADXT_StartMem` equivalent also requires the actual available size.
For `ADXT_StartMemIdx`, select the ACX entry and use
`kosdec_adx_source_init_memory_joint` with the exact resident loan.
The joint is finite and reports the selected bounds to the player.
The 2.00J unsized and indexed calls use a one-GiB extent sentinel.
The native indexed selector preserves the last valid view on failure.

`adx_files.h` owns up to32 named inputs per caller-owned collection. Filenames are
copied; IDs are never reused during that collection's lifetime. File source loans
use private synchronous descriptors and the existing optional read cache. Return
is checked by exact descriptor identity. Busy loans prevent close. Close-all
continues through independent handles and reports retained slots separately from
consumed closes whose cleanup failed. Never retry a consumed close.

A filename loader composition is explicit:

1. Open through files and borrow the source descriptor.
2. Optionally select ACX/AFS membership through source, or load/register an AFS
   directory. `partition_resolve_view` chooses both the resolved overlay range
   and its original archive provider; copied views survive metadata closure.
3. Pass the resulting bounded provider to the stream/sequence, or enqueue it in
   the loader with sector0 and enough sectors to cover its byte extent. The
   loader clips the final sector without publishing padding. Labels are copied
   metadata, not a second filename resolver.
4. Stop/join consumers, close loader/reader objects, and separately retire any
   external transport. Return selected-source loans before the parent file loan,
   then close the file. Closing directory metadata does not release those loans.

There is no process-global directory, file pool, VFS reset or implicit shutdown.
Explicit KOS paths replace a hidden current-directory API. Direct-disc providers
retain their existing separate request-destruction requirement. Owned filenames
remain restricted to immutable synchronous VFS backends.

`LSC_SetFlowLimit` and `LSC_GetFlowLimit` map to the selected LSC policy
and the requested limit in the loader snapshot. The active source keeps
its captured threshold. The buffer snapshot reports the effective value.
Batch admission uses queued DATA and a 512-sector cap. The native loader
services each batch through bounded provider calls and exposes incremental
progress. Stock cvFs completion visibility remains a separate gate.

For `ADXT_StartSeamless`, select the linked player reload profile and
bind a fresh reader with a stopped loader. The player stages the cached
`ADXT_SetReloadSct` or `ADXT_SetReloadTime` equivalent before start or
source rebind. Changing the player cache while linked playback runs
affects the next linked start. The controller's direct LSC setter
remains a producer-owned operation with per-source capture.

The player's input snapshot provides queued bytes, whole sectors,
nominal seconds and safety for ADX. It excludes held joint DATA and
uses only locally staged bytes for a read-at stream or active sequence
entry. Time uses whole groups at the validated source rate. Safety
uses the live player cache. The producer publishes the byte count and
format phase together. The `ADXT_GetInputSj` equivalent returns a const
identity only when the native source uses a joint.

## Lifecycle and service

`adx_group.h` collects up to32 initialized players. Poll visits a bounded number
of slots in round-robin order; a failing player does not prevent independent
players being serviced. Stop-all attempts each checked software shutdown and
retains failures for retry. Removing a stopped player returns its snapshot,
including retained sound ownership. Only an empty collection can finish.

The native scheduling contract is bounded work and explicit wakes: each decoder
step performs at most one provider read and one32-frame group or512 metadata
bytes. The player uses its private fiber/service executor, respecting the fork's
exclusive provider selection. Input WAIT, output backpressure and pause park or
reschedule the producer; control and cancellation use the existing mailbox/wakes.
Group polling adds owner-side fairness, not another decoder worker. A blocking
synchronous provider can still delay its owner; use the nonblocking direct-disc
provider where required. No VSync-frequency shim or global sound initializer is
hidden in these APIs. Exact SDK server-call timing is not certified by this model.

Applications with synchronous service phases can opt into
`adx_compat_service.h`. Each caller-owned collection has one shared watchdog
clock. Only explicitly provided phases run, in decoder, renderer, file and
stream order, then occupied slots in ascending index order and finally
linked reload. A title dispatches its entry state once. Its post-dispatch
state and decoder snapshot drive the admitted watchdog check. Direct title
service uses the same clock. Multiple collections have independent clocks.

To service ADX on VBlank events, call `kosdec_adx_compat_exec_vsync` once
for each event delivered to the ordinary owner. It advances the
collection's wrapping count before running the usual server pass. Regular
server and single-title calls leave that count intact. Enable
`kosdec_adx_compat_enable_vsync_clock` before binding titles if their
compatibility time should use this count. The native clock keeps its own
optional reader, and a live compatibility query still needs qualified
source attribution. Hand VBlank events off to the owner outside IRQ.
Rebuild consumers of the collection with the matching headers and archive.

A standalone loader can use split service under the collection owner.
In STREAM, call `kosdec_adx_loader_step_stream` to progress one previously
admitted request. DATA published there is available to title dispatch.
In RELOAD, call `kosdec_adx_loader_step_reload` to complete a finished read
and admit the next request without calling the provider. No FILE phase
is required for this composition. Keep the loader, joint consumer and
controls on that same owner. A loader bound to a private player producer
continues to use the combined step.

Use `kosdec_adx_compat_bind_phased_player` for finite read-at ADX, WAVE,
AIFF, AU and SPSD, ADX sequences and batched joints backed by sealed
resident memory, a linked loader or a sector provider. DECODER consumes
available DATA within the DECINFO frame budget. A sealed MEMORY joint
starts with DATA already available and requires no transport phase.
RENDERER polls active output once. FILE uses a 2048-byte ADX window.
For WAVE, AIFF, AU and SPSD, FILE stages PCM input in a 4096-byte
reader window using at most sixteen requests of up to 512 bytes each
per visit. A header visit still makes at most one read up to the next
parser boundary. DECODER parses headers and converts staged samples.
A provider error after a complete buffered prefix reaches DECODER
after the prefix. The window covers 48 kHz stereo PCM16 at one pass per
nominal PAL or NTSC VBlank when the provider and renderer keep up. The threaded compatibility binding continues to use the source's
ordinary reader on its producer fiber. STREAM advances a sector-backed
request or a linked loader after FILE. RELOAD can finish or arm linked-loader
requests. Sector and loader transport phases use up to four bounded steps
and stop on a wait or backpressure. Title dispatch runs once after FILE and
STREAM. PREP can admit initial output. Direct title service uses
existing snapshots without running collection phases.

Compatibility DECINFO rounds the target using 32 frames per ADX
block, four per packed WAVE block and one for the supported PCM and AU
mu-law inputs. PREP marks a format-validated source ready when it reaches
the captured target, leaves at most one block of PCM room, or finishes
decoding. A wait-start hold lets readiness become visible
before playback. Native playback still primes its sound buffer, and an
explicit release renews that bounded priming window if more PCM is needed.

Use `kosdec_adx_compat_player_is_ready_start` to read the scoped stock
latch. It remembers the last preparation across STOP and source release.
A new admitted start clears the latch. The ordinary readiness query
continues to describe preparation of the currently owned source.

An explicitly paused compatibility title can prepare before output
admission. Open the separate wait-start gate to let the prepared title
report logical PLAYING. The native player stays in PREFILL with no
output allocation and zero elapsed source frames. Sample time uses the
admitted source rate, and VSync time stays at the initial anchor. Clearing
pause lets the next title visit start output once the normal PCM priming
condition is met. Checked STOP preserves the pause request. Active-device
pause is still an Audio dependency. With the pause request clear,
`kosdec_adx_compat_player_discard_samples` returns zero without service
or clock changes. A paused discard returns ENOTSUP before touching either
queue or any clock state.

The compatibility time query returns the signed display offset before
start and after checked STOP. The selected denominator is 1 for source,
5994 for NTSC, or 5000 for PAL. A literal query needs no tick reader. A
running query still needs qualified clock data. The compatibility clock
uses the stock signed 32-bit counts and saves its VSync anchor only when
drift triggers a correction. The native API still uses its independent
wide clock and requires a generation. The compatibility centisecond query
uses the SH-4 binary32 conversion. Rebuild consumers of the player struct
with the matching headers and archive.

For a final retained source, opt in with
`kosdec_adx_compat_player_enable_playend` before the first binding starts,
or after releasing a sealed source in IDLE to mark the next binding final.
The collection must have only one occupied title to advance the transition.
Source DONE and an empty software pipe allow sealing. Every generation on
the retained lease must report qualified sealed source-output end on the
same session, epoch and cursor observation. An empty successor does not
substitute for proof of earlier output traversal.
Compatibility state becomes PLAYEND and IsCompleted becomes true only
after checked stop and join. An expired stop deadline leaves the same
transition pending. Source release, cancel and explicit STOP disarm it.
An explicit STOP after PLAYEND returns the compatibility view to STOP.
The legacy terminal time uses declared source frames and the saved VSync
correction anchor. Terminal queries never poll or advance that anchor.
Logical completion leaves played sound resources retained. The default
player continues reporting its existing source-copy boundary.

Use the compatibility attenuation setter and getter for stock requested
volume readback. The title keeps the signed low halfword, including before
start, through checked STOP, and on live admission failure. A new binding
starts at zero. Recovery that replaces the player in the same slot keeps
the setting. Request ID zero confirms a cached request. The initial
attenuation opt-in uses the signed cached value at the first retained
START. A live submission uses the original full request and its nonzero
ID can be checked with the native acknowledgement query. The native
requested getter reports the last locally admitted live device request.

For mono or stereo initial attenuation, build with `ADX_RETAINED_CLOCK=1`
and `ADX_RETAINED_START_TL=1`, select the retained clock, and call
`kosdec_adx_compat_player_enable_initial_attenuation` on the READY title.
The first lease applies the stock zero-fade TL for the signed cached
volume before synchronized key-on. A reused output keeps its current
device setting. A replacement title can opt in separately.
Firmware without the checked TL capability fails startup with ENOTSUP.
A failed close of an unstarted lease is reported in `stop_error` while
`first_error` records startup ENOTSUP. STOP retries that close. A failed
attempted start retains the lease through checked STOP.
Fades, device gain on transitions, and physical mute remain open.

The non-retained compatibility player accepts direct and FX send levels
in whole dB from -45 to 0 after clamping. The stock bus-16 alias reads
back as 16 and reaches KOS as bus 0. A level-only FX adjustment preserves
the configured bus; direct and FX controls preserve each other's fields.
Without live output, a successful setter stages the full channel tuple.
With live output it submits a routing-only update for the active channels
in order. The return value distinguishes staging and command admission.
If a later channel fails, an earlier command may already have been
admitted. The player reports the error and requires checked STOP.

Getters report the title's requested settings, including a failed live
request. Defaults are direct 0 dB, FX bus 0 at -45 dB, and transpose and
detune at zero. The values persist across checked STOP and source rebind.
Same-slot recovery carries requested settings and previously staged or
admitted fields to non-retained output. Unbind restores the native
configuration and clears the compatibility settings.

Use `kosdec_adx_compat_player_set_initial_transpose` on a READY ordinary
player to stage a zero-detune setting of −12, 0 or +12 semitones for a
44.1 kHz source. The selected output rate is 22,050, 44,100 or 88,200 Hz.
The source rate and frame counts are unchanged. A nonzero explicit
native rate takes precedence. Rejected requests leave the prior staged
setting and are still available through requested transpose readback.
The cache-only setter does not stage an output rate. Cancelling before
allocation clears staging. Same-format source reuse keeps the active
output rate, and replacement players need their own initial setting.
Live transpose, finer offsets, other source rates, retained routing,
applied stereo synchrony and physical DSP output remain open. Rebuild
player users with the matching header and archive.

At 88.2 kHz the verified runtime used a fiber-backed producer, a 32,768-frame
decoded PCM queue, a 16,384-byte sound ring per channel and a 1 ms sleep
between service passes. The 2 ms phased producer fixture at that rate
reported a refill shortfall in the SH-4 interpreter.

For independent file handle requests, initialize caller-owned slots with
`kosdec_adx_compat_files_init` and open a slot over a borrowed source.
Register `kosdec_adx_compat_files_observe` in FILE and
`kosdec_adx_compat_files_stream` in STREAM of the same service collection.
A logical read can write a flat buffer or publish whole sector batches to
a joint. The default batch cap is 512 sectors. STREAM may finish while the
file state remains READING. The next FILE visit commits its sector cursor.
Stopping first preserves the old cursor, reports completed sectors, and
returns any unpublished joint storage. The caller retires its provider and
returns its source loan after closing the independent file slot. When a
provider can identify retryable errors, the optional classifier enables a
whole-batch retry and the stock 12-failure limit for those errors.

A finite loop rewind discards the old staged bytes and any error from a
prefetched offset the decoder no longer needs. A pending native seek
uses FILE for input and DECODER to reconstruct history. The standalone
stream read retains its single 512-byte provider call and scalar group
limit. Rebuild consumers for the expanded stream and sequence storage.

All parts belong to the same ordinary owner, including the provider,
joint, player controls, and loader. Configure a linked loader before
starting or use the existing player-linked reload configuration.
Sector loop release takes effect at the provider's loop boundary. An
already-prefetched rewind can still contribute PCM. After checked source
release, cancellation, or stop, close the reader and retire its provider
separately. Rebind and recovery can supply any supported phased source.
Keep provider calls within the owner's service budget. Player init
requires the supplied stack.

Bind adapters that can observe completed work. Waking a producer fiber does
not constitute a synchronous decoder phase. Give each title one service
owner. At format validation, a synchronous adapter can use the prefill
query to set its block-rounded preparation target. Watchdog checks report
low compressed DATA and a stationary decoded position separately from
callback errors. The same clock opportunity can be consumed by an occupied
STOP slot. Existing native players keep their configured prefill and
elapsed-time stall policy. The optional collection does not establish
a device-drain or audible-completion guarantee.

The optional producer progress query reports the cumulative decoded-frame
count used for stall detection, the current logical source frame, staged
DATA bytes and the admitted format, including frames per block. Container
metadata is excluded from the DATA count. A loop rewinds the logical frame. A seek
can move that frame before any PCM is published. The cumulative count resets
on each player rebind. Enable the query before start and retry EAGAIN on a
later owner call if the producer is publishing. Rebuild service and player
consumers against the matching headers and archive.

Bind an initialized native player with `kosdec_adx_compat_bind_player` before
starting it. Drive it through its collection until checked stop and unbind.
The direct player poll rejects a bound player, which prevents servicing it
through both collection types. Keep the player and collection alive through
unbind. The same binding can rebind a source using the normal ownership rules.

DECINFO captures the frequency-based target at admitted ADX format and
releases the producer to begin PCM reservations. Until that handoff, the
producer can parse the header and handle controls or cancellation. PREP
uses the captured target and the Audio first-transfer minimum, with the existing
validated short-source path and prepared-start gate. Frequency updates change
the next admitted watchdog and the next source's target. Once captured, the
target also caps each newly admitted producer PCM reservation. An in-flight
job can finish under the bound it sampled on entry. A full live ring that
cannot hold the target fails with ENOBUFS before starting output. The native
asynchronous producer still owns decoder/transport stepping. Only completed
synchronous phase callbacks can appear in the processed phase mask. The
watchdog uses a completed producer publication or reports a deferred sample.
For a short source copied entirely during PREP's initial refill, the
compatibility title enters PLAYING first. Its next PLAYING dispatch can
publish SOURCE_END. PCM copy and retained sealing keep their existing
boundaries.

## Diagnostics and recovery

Error query/clear and explicit owner notification remain independent of safety
errors and stop state. `adx_recovery.h` adds NOACT, STOP and bounded REPLAY policy.
Its owner supplies nondecreasing monotonic milliseconds. STOP schedules checked
shutdown after a configured delay; failed stop retains resources and retries.
REPLAY accepts terminal source IO_ERROR and the optional running input watchdog,
with a finite retry count. To enable the watchdog for a continuously supplied
source, set `input_stall_timeout_ms` in the player config. Repeated short refills
during one continuous input wait cause ETIMEDOUT and set `input_stalled` in the
player snapshot. Input progress, production pause, a full refill and terminal
status disarm the timer. Malformed input and unrelated timeouts take the stop
path. The threshold uses native monotonic milliseconds and diagnostics use errno.

Replay preparation runs once after successful software stop/join. The callback
receives the old retained sound handle and must retain it until Audio permits
reclamation. It must retire/rewind input and supply a properly initialized READY
player. Fresh storage remains caller-owned on every failure. No callback means
REPLAY cannot be admitted. Reinitializing an active player, resetting sound, or
freeing its sound buffer is never an implicit recovery step. Repeated fresh
allocations can exhaust sound resources; the bounded policy is not a reuse proof.
Normal source end is neither an error nor proof that the last sample played.

For a compatibility-bound player, dispatch the collection before calling
`kosdec_adx_recovery_step_compat` on the ordinary owner. The signed
watchdog query retains the legacy numeric result. The separate fault mask
captures low DATA, stagnant decode and input transport failure on the
latest admitted check. The phased owner can see a failed FILE read
before DECODER reaches the failed byte. With NOACT, valid staged bytes
remain available to the decoder. A transport fault alone leaves the
signed watchdog code unchanged. The collection reports a failed dispatch
with its original errno. Malformed or truncated input does not set the
transport bit.

NOACT retains the reasons without acting on the binding. STOP performs
checked shutdown. REPLAY can prepare a fresh READY player in the same
slot for isolated low DATA or transport failure. A stagnant decode takes
STOP precedence, including when it coincides with either reason. The
recovery snapshot retains the reasons and original cause across a stop
retry. After a successful stop in this case, restarting requires explicit
owner setup. This differs from stock REPLAY, which can attempt to restart
the joint cleared by the stagnant-decoder stop. The optional cadence,
checked software stop and retained allocation give separate evidence.
Rebuild recovery and compatibility consumers against the matching headers
and archive.

For recovery within a phased title visit, attach a separate controller with
`kosdec_adx_recovery_attach_compat`. Stage an independent READY successor
outside service if REPLAY may be needed. An admitted condition runs the
same-call checked stop before later titles and reload. A timeout retries that
stop on the next visit, using the original fault snapshot. An explicit player
stop cancels pending replay. A stagnant decoder still takes STOP precedence.
REPLAY can admit only the staged successor, after stop succeeds. The new
binding keeps the title slot, frequency and collection cadence. Old sound
stays retained. Detach the controller before unbinding the title. This
profile uses caller-prepared storage and performs no provider reopen callback
inside service.

## Queues and linked records

The existing PCM sequence retains strict same-format validation, bounded enqueue,
repeat/release and per-entry predictor reset. An atomic service queue-count query
and player snapshot report pending entries, including the active entry at the
counted stage. A bound loader counts pending reads. A PCM sequence counts
pending decodes. Neither count measures audible progress.

Opt-in `stream_set_linked` accepts adjacent complete ADX records. Each footer is
validated before the next header; histories reset for each record. Rate/channels
must stay fixed. Embedded loops and linked seeking are rejected. Ordinary streams
still reject trailing records. Producer-side `RELEASE_LINK` finishes the current
record; an atomic service query/player snapshot reports the follow setting.
Both the stream and linked joint validate exact header and footer boundaries.
The joint can bind the native loader over the same compressed ring. Loader
repeat rotates the descriptor at transport completion and keeps its source ID
and ownership. The decoder can start the next linked record while earlier PCM
remains queued. Native loader steps stay bounded to one provider call.
For live registration, the ordinary owner opens a named file or selects an
AFS member, then posts its borrowed descriptor with `LOADER_ENQUEUE`. The
producer returns admission and the assigned source ID in the one-shot receipt.
`LOADER_FINISH` closes registration, disables repeat and lets the queue drain.
Keep accepted source loans through producer join and backend retirement.

Optional linked scan snapshots two DATA spans when a footer validates. The
next header must already fit in those spans, at an even offset in its own
span. This permits transport padding and supports the stock next-header
cutoff. Default linked input waits for the next adjacent header.

A joint consumer can use `kosdec_adx_joint_get_next_data` to hold two adjacent
logical DATA spans, including a ring wrap or a queue head boundary. Consume the
oldest prefix first and return the newest suffix first. Each queue loan reserves
its own node. Reset and close still require both spans to be returned. Audible
transitions and physical drive timing remain to be qualified.

## Live SFA framing

`adx_sfa.h` publishes a validated288-byte prefix, complete compressed body groups,
and an18-byte endcode to a live ring. The declared frame count is known at init;
normal and inserted zero groups share one exact length budget. Only after that
budget is filled does pump publish the footer and seal input. Steps are bounded
to512 bytes and report backpressure, including split headers and ring wraps.
A joint reader may consume between calls or invoke the writer from its feed hook;
all joint mutations remain serialized on that producer.

When setup precedes the final length, use `kosdec_adx_sfa_init_live` to pair the
writer with its joint reader. The writer publishes a placeholder header and
accepts whole groups while the producer determines the length. Call
`kosdec_adx_sfa_set_length` once with the authoritative sample count before
publishing a partial final group. The paired reader uses that count for output.
Unpaired readers validate ordinary stream lengths as before. The mux or demux
integration must determine the count and supply it on the serialized producer.

Zero residual groups preserve predictor history and may produce nonzero PCM.
Output-side sample discard and playback-time amplitude lookup remain Audio work.

## Baseline dispositions and remaining qualifications

The supplied header disables `ADXF_GetFileRange` with `#if0`; the active
GetFnameRange variants remain mapped to native directory/provider operations.
The supplied library's `ADXT_ReleaseSeamless` is an empty return; native sequence
repeat release is explicit useful behavior, not an inferred SDK implementation.
The header and archive disagree about whether CloseAllHandles also closes files;
separate checked player/file collections make both operations explicit.

Native namespaced lifecycle/service/recovery is the accepted native interface for the
three MWPLY ADX facade declarations. No MWPLY vtable or ABI facade is implemented.
This accepts the interface design, not unimplemented runtime behavior or timing.
API declarations alone do not prove a matching SDK facade implementation.

Host tests distinguish control models, actual codec/player composition and Audio
mocks. SH-4 compile/link proves target consumption, not physical sound retirement,
multi-player hardware behavior, audible seams or performance. Those qualifications
and all remaining COMPLETION.md rows still apply.
