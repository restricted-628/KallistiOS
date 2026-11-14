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

## Diagnostics and recovery

Error query/clear and explicit owner notification remain independent of safety
errors and stop state. `adx_recovery.h` adds NOACT, STOP and bounded REPLAY policy.
Its owner supplies nondecreasing monotonic milliseconds. STOP schedules checked
shutdown after a configured delay; failed stop retains resources and retries.
REPLAY applies only to terminal source IO_ERROR, with a finite retry count.
Malformed input, prefill timeout and other failures stop without assuming a disc
fault. No running-input stall detector or SDK numeric-code translation is claimed.

Replay preparation runs once after successful software stop/join. The callback
receives the old retained sound handle and must retain it until Audio permits
reclamation. It must retire/rewind input and supply a properly initialized READY
player. Fresh storage remains caller-owned on every failure. No callback means
REPLAY cannot be admitted. Reinitializing an active player, resetting sound, or
freeing its sound buffer is never an implicit recovery step. Repeated fresh
allocations can exhaust sound resources; the bounded policy is not a reuse proof.
Normal source end is neither an error nor proof that the last sample played.

## Queues and linked records

The existing PCM sequence retains strict same-format validation, bounded enqueue,
repeat/release and per-entry predictor reset. An atomic service queue-count query
and player snapshot report queued decode entries, including the active entry.
They exclude audio already consumed from the sequence and are not audible clocks.

Opt-in `stream_set_linked` accepts adjacent complete ADX records. Each footer is
validated before the next header; histories reset for each record. Rate/channels
must stay fixed. Embedded loops and linked seeking are rejected. Ordinary streams
still reject trailing records. Producer-side `RELEASE_LINK` finishes the current
record; an atomic service query/player snapshot reports the follow setting.
It never scans for a plausible next header or silently changes devices.
The reader's staging handles split records without multiple simultaneous joint
DATA loans. This differs from the SDK's joint mechanics. Audible seamlessness,
exact prefetch timing and unrestricted multi-borrow SJ compatibility remain open.

## Finite live SFA framing

`adx_sfa.h` publishes a validated288-byte prefix, complete compressed body groups,
and an18-byte endcode to a live ring. The declared frame count is known at init;
normal and inserted zero groups share one exact length budget. Only after that
budget is filled does pump publish the footer and seal input. Steps are bounded
to512 bytes and report backpressure, including split headers and ring wraps.
A joint reader may consume between calls or invoke the writer from its feed hook;
all joint mutations remain serialized on that producer.

Zero residual groups preserve predictor history and need not sound immediately
silent. This is not an encoder. Unknown-length SFD input, demux-controlled live
length revisions, output-side sample discard and playback-time amplitude lookup
are not supplied by the finite writer.

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
