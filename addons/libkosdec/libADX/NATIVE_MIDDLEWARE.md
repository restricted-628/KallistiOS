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
