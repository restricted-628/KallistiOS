# libADX foundation

This is an independently written, incremental float ADX decoder. It accepts
encoding 3, versions 3 and 5, 18-byte channel blocks, 4-bit residuals, mono/stereo
at integer rates 1..48000 Hz with integer cutoff 0..floor(rate/2), a 26..65539-byte header,
and an 18..65539-byte zero-padded end record. Declared length is limited to INT32_MAX
PCM frames. Metadata becomes valid when `header_ready` is true; use
`kosdec_adx_get_info` for a validated snapshot.

Headers may have zero extensions or a validated version-3 loop record:
0..31 inserted leading samples, one type-1 loop, block-aligned start, ordered sample
bounds and matching absolute byte offsets. Other extensions remain unsupported.
48 kHz is our extension beyond the baseline ADX Talk header's 44.1 kHz maximum.
Out-of-domain rates/cutoffs, other versions/encodings and nonzero footer payloads
remain unsupported. This is not complete ADX middleware parity.
The native player accepts effective output rates 8000..48000 Hz; lower-rate
codec sources require an explicit in-range playback-rate override (changing pitch
and duration). Default low-rate output fails with ENOTSUP before sound allocation.
Restoring the default rate on such a source fails with EINVAL without changing output.

The codec decodes each file once, including files with loop metadata. It exposes
`decoder.loop` for the optional memory reader; it does not automatically repeat or
seek. `end_frame` is exclusive. `end_byte` follows the compressed block group
containing the final loop sample, which can contain samples beyond the loop end.
The optional resident-input reader implements repeat/release with carried history.

## Use and ownership

Initialize caller-owned `struct kosdec_adx_decoder` with `kosdec_adx_init`.
Call `kosdec_adx_decode` with input bytes and an interleaved signed PCM16 buffer.
Advance input by `result.consumed` and output by `result.frames * channels` samples.
Output capacity is in frames, not samples or bytes: reserve `frames * channels`
PCM16 elements. Normal calls process at most one 32-frame group or 512
header/footer bytes. Large metadata can return `MORE` with unread input even when
`final_input` is true; advance by the reported byte count and keep draining.

Input may be fragmented arbitrarily. Keep the decoder, input and output disjoint,
output naturally aligned, and each instance serialized. Reassert `final_input`
while draining the last span. `NEED_OUTPUT` means more capacity is required;
it does not indicate truncation. `NEED_END` waits for an explicit end of input.
`DONE` validates both declared sample completion and the end record. Terminal
errors/cancellation remain sticky until reinitialization. Cancellation is between
calls. No calls allocate, block, yield, invoke callbacks or access hardware.

## Metadata and record inspection

`kosdec_adx_get_info(&decoder, &info)` returns `DONE` with a
`struct kosdec_adx_info` snapshot of header bytes, total PCM frames, sample rate,
cutoff, inserted leading frames, encoding, block bytes, sample bits, channels,
version and loop metadata.
Before the header is complete it returns `NEED_INPUT` or the decoder's terminal
error. Once admitted, metadata remains available after a later body error or
cancellation. This is not a live playback-position query. Serialize access with
decoding, and keep the output disjoint from the decoder.

`kosdec_adx_probe_header(input, bytes, &info)` validates a contiguous header prefix
using the same parser. `kosdec_adx_probe_endcode(input, bytes, &record_bytes)`
validates an end-record prefix, including its declared length and zero padding.
Both return `DONE` for a complete supported record, `NEED_INPUT` for a prefix
that needs more bytes, or the normal malformed/unsupported status. Outputs stay
unchanged unless `DONE`; bytes after the validated record are ignored. A successful
probe does not validate the compressed body or actual file EOF. Use incremental
decoding or `adxcheck` for full-file admission.

Probes accept byte-aligned input and allocate no memory. They inspect at most
65539 bytes in parser steps of at most 512 bytes; choose the incremental decoder
when a caller needs a single-step work bound. These are intentionally strict
native queries, not CRI ABI recognizers: the shipped end-code recognizer checks
the marker and reports the supplied span, whereas our query reports the validated
declared record size. Format support and PCM arithmetic are unchanged.

## Encoder padding and SFA prefixes

The encoder can align a requested loop start by inserting up to 31 leading PCM
frames and adjusting the encoded loop positions. `info.inserted_frames` exposes
that metadata. The decoder returns the declared encoded timeline as stored;
it neither removes those frames nor synthesizes extra silence. A padding count
larger than the loop start is invalid. Truly non-block-aligned encoded starts
remain unsupported: a requested source-sample start and its adjusted encoded
position are different coordinates. This does not change carried loop history.

`kosdec_adx_make_sfa_header(output, capacity, channels, rate, frames)` constructs
a 288-byte SFA ADX prefix: encoding 3, version 3, 18-byte blocks, 4-bit residuals,
500 Hz cutoff and no loop. It returns `DONE` after writing the prefix, `NEED_OUTPUT`
for insufficient capacity, or `UNSUPPORTED` for parameters outside the decoder's
admission domain. With the fixed cutoff, admitted rates are 1000..48000 Hz and
channels are mono/stereo. Output is unchanged on failure; surplus capacity stays
untouched. Feed the generated prefix before the matching headerless ADX body.
A complete bounded file still needs its end record. This function does not encode
PCM, alter an active stream, or implement an SFD demuxer/SJ insertion facade.

Consumers must rebuild for the expanded metadata snapshot structure. Existing
decoder, reader and player state layouts and PCM arithmetic are unchanged.

## Resident-input PCM reader

Include `<kosdec/adx_memory.h>` and initialize caller-owned state with an immutable
whole-file span and a repeat request. `kosdec_adx_memory_read` returns interleaved
PCM in capacities as small as one frame. It decodes at most one 32-frame group
per call, retaining up to 128 bytes of PCM. Header/footer work is limited to
512 bytes per call. A paused read returns `MORE` without progress; the caller
controls when to resume. Cancel and terminal results are sticky.

Repeating requires supported loop metadata. The next nonempty read at the
exclusive loop endpoint discards cached samples beyond that endpoint, seeks to
the loop start, and carries the full decoded group's predictor history.
`kosdec_adx_memory_release_loop` permanently disables future wraps for that
initialization; any cached tail is retained and playback data continues to EOF.
The reader decodes full compressed groups, including terminal padding, to retain
whole-group history. It never returns padding. This differs deliberately from the
single-pass codec's final partial-group history policy.

`source_frame` identifies the next source frame to return. `returned_frames` and
`wraps` count PCM returned and loop transitions, not samples consumed by hardware.
These unsigned cumulative counters wrap modulo 2^64. Input remains borrowed until
completion/cancellation/reinitialization. The entire compressed body and minimum
footer extent must be resident before PCM is returned; block contents and footer
are validated when reached. Repetition does not imply that an unvisited tail or
footer has been validated. A final PCM return is followed by footer work before
`DONE`. There are no allocations, callbacks, scheduling or device operations.

## Streaming and KOS files

`<kosdec/adx_stream.h>` exposes the same loop/history policy through an original
absolute-offset input callback. The reader owns a 512-byte compressed window and
128-byte PCM cache, retains partial groups, and consumes at most one group per
call. A read invokes its provider at most once, requesting at most 512 bytes.
Positive short reads advance input; `INPUT_WAIT` transfers nothing and returns
`NEED_INPUT` to the caller. There is no busy retry or hidden scheduling. `MORE`
can mean partial-input progress with no PCM. A zero-capacity call performs no I/O,
including header/footer work. Pause and terminal/cancelled calls also suppress I/O.

Source size is fixed and limited to UINT32_MAX bytes. Providers copy into the
supplied destination synchronously and must never retain it, including after
WAIT or an error. An asynchronous transport needs separate provider-owned staging
and its own retirement contract. The stream reader does not cancel DMA, close
sources or wait for external workers. Serialized cancellation is between calls.
Early EOF is `TRUNCATED`; backend failures or invalid provider result lengths are
`IO_ERROR`. Existing status enum values are preserved.

Loop rewinds discard compressed read-ahead, preserve full-group history and
request the exact validated start byte. The rewind is committed before that
request, so a subsequent WAIT/error can occur with `wraps` already incremented.
Releasing then continues the new iteration to EOF.

For an unbound nonrepeating stream, `kosdec_adx_stream_seek_begin(stream, frame)`
starts a sample-based seek. It resets decoding with the same provider/extent and
unpauses, without performing I/O. `seek_step` reconstructs predictor history from
the beginning, discarding at most32 frames per call with at most one512-byte
provider read. It returns MORE/NEED_INPUT until DONE means the position is reached.
A pipe/service automatically advances a pending seek on its producer fiber before
publishing PCM. Work is proportional to the target position; provision the player's
prefill timeout accordingly. There is no compressed-block index or constant-time
seek guarantee. Normal reads reject a pending seek.

Seek zero, mid-block and EOF positions preserve the same full-group math as linear
stream decoding. A seek to EOF does not validate the footer until the next normal
read. Bounds known at begin are rejected without mutation; otherwise they are
checked after header parsing. Errors/cancellation remain terminal until seek/init.
Returned-frame/wrap counters reset and exclude skipped samples. This is a source
position, not audible time. Convert a desired time to a source sample position
using the decoded sample rate, independent of output pitch.

Repeating readers return UNSUPPORTED: release their loop first, or reinitialize a
terminal repeating reader with repeat=false. A player/service must release or
cancel and join before its source is manipulated. Keep the provider and its async
staging alive; seeking does not close/cancel provider I/O or reclaim any buffers.

For KOS VFS, include `<kosdec/adx_file.h>` and pass an already opened descriptor to
`kosdec_adx_file_init`. Read/pause/release/cancel through `file.stream`. The caller
owns the descriptor and must keep it alive and serialize all cursor access,
including through duplicate descriptors. The adapter queries `fs_total64`, then
uses `fs_seek64` and `fs_read`; exact returned seek positions are checked, including
handlers with narrower offset support. It captures failures in `file.io_errno`
and retries EINTR/EAGAIN only on a later caller invocation. The descriptor is never
closed by this library. Files must stay immutable during use.

For streamed files, `kosdec_adx_file_init_buffered` optionally borrows a
512..32768-byte compressed-data cache. For example, keep the following objects
alive until the player/service and all other source users have joined:

```c
struct kosdec_adx_file file;
_Alignas(32) uint8_t compressed_cache[16384];
enum kosdec_adx_status status = kosdec_adx_file_init_buffered(
    &file, descriptor, repeat, compressed_cache, sizeof(compressed_cache));
/* On MORE, use file.stream exactly as with the unbuffered binding. */
```

A cache hit copies compressed bytes without a filesystem call. A miss performs
at most one seek/read, bounded by the cache capacity and remaining file extent.
Positive short reads are supported; interrupted/would-block reads yield to the
caller. Seeking and looping reuse immutable cached bytes when available. This
reduces small filesystem requests without decoding the whole file to PCM or
allocating memory. The original init remains unbuffered. Keep the cache disjoint
from all other live buffers and satisfy the selected VFS handler's alignment and
transport requirements. All objects that include `kosdec_adx_file` must be rebuilt
against the updated header; the struct now holds cache state.

This adapter may block in the calling thread/fiber. Do not call it from an IRQ or
a time-critical sound refill callback. It uses the opened VFS handler's existing
transport policy, without selecting BIOS/DMA modes or changing shared interfaces.
The file object is a separate archive member: codec/memory/custom-stream consumers
do not pull in VFS dependencies. Host tests use a FILE-backed VFS signature fixture;
that fixture is not installed and does not establish real KOS filesystem execution.

## File music integration

For a complete ordinary-file music consumer, see
[`examples/file_audio.c`](examples/file_audio.c). Build it with
`make file-audio-example` using the fork's configured SDK. Supply your own
`/cd/test.adx`, or pass a path as the last argument. Default playback is once;
`--loop` uses embedded supported loop metadata; `--repeat` repeats a whole
non-looping file through the sequence service. Whole-file repeat resets predictor
history; embedded looping carries the established full-block history. Press A to
release repetition and START to stop. Unsupported assets fail explicitly.

The example uses 32 KiB compressed read-ahead, a 16384-frame stereo-capable PCM
ring, a 16 KiB producer stack and 16 KiB of sound buffering per channel. Only the
producer performs file I/O; poll/refill copies bounded PCM. The application owns
global sound initialization and calls the player from one ordinary owner thread.
Source EOF joins the producer and closes the file while output continues polling
tail/silence. Explicit stop joins CPU users and retains sound allocations. It does
not use a guessed tail delay, claim audible completion, or reclaim sound RAM.
The example leaves output rate unchanged and does not exercise live-rate changes.

The host `file_player` regression runs the real file/stream/sequence/service/player
composition with a pthread producer and modeled output sink. Mono/stereo,
22.05/44.1 kHz, short reads/retries, once/loop/repeat, release, seek/rebind, source
failure and cancellation are checked against resident-reader PCM. Host passes and
native links do not establish target timing or audibility. Use the accepted Audio
backend prerequisites; the known native cooperative-getter and live-rate issues
are not repaired by this example or by the compressed cache.

## Asynchronous direct-disc input on KOS Experimental

`<kosdec/adx_disc.h>` adds a fork-native source for the stream reader and indexed
lookup. It borrows an explicitly direct `cdrom_sector_range_t`, a fixed byte
subrange and caller-provided 32-byte-aligned staging of 2..32 KiB (whole sectors).
It submits asynchronous DMA through the fork's existing request engine. It does
not switch the ISO backend, issue controller commands or choose a BIOS fallback.
The range must be idle and exclusively borrowed; the media must remain unchanged.

Zero-initialize `struct kosdec_adx_disc`, then initialize it with the exact source
byte length, excluding sector padding. For a plain ADX file:

```c
/* range and staging are already provisioned by the application. */
struct kosdec_adx_disc source = {0};
struct kosdec_adx_stream reader;
if(kosdec_adx_disc_init(&source, range, byte_offset, file_bytes,
                       staging, staging_bytes, 4000) == KOSDEC_ADX_MORE) {
    kosdec_adx_stream_init(&reader, kosdec_adx_disc_read_at,
                          &source, file_bytes, repeat);
}
```

For ACX/AFS, initialize the index with `kosdec_adx_disc_read_at` and `&source`,
then decode the resulting member view. Unaligned member offsets are handled by
the byte view and staging; no unaligned CPU loads or DMA destinations are needed.
The provider reads ahead at most 16 sectors, retains that cache between calls,
and copies at most 512 logical bytes per request to the stream/index destination.
Sector padding and neighboring members never become logical decoder input.

On a cache miss it submits once and returns `INPUT_WAIT`. Later invocations poll
once; they never spin or retain the caller's destination. Even terminal status
must be followed by successful request destruction before payload is published or
staging reused. Errors are sticky, recorded in `io_errno`, and discard any partial
payload. A changed input offset waits for the old request to retire before a new
request can replace the cache. This is bounded read-ahead, not a double-buffered
background prefetch service. Submission may allocate driver bookkeeping and take
short locks; keep it outside IRQ and time-critical audio callbacks.

After decoder completion, error or cancellation, stop its reads and repeatedly
call `kosdec_adx_disc_close`, scheduling between attempts. Only `DONE` releases
this binding's request/staging ownership. `NEED_INPUT` and `IO_ERROR` retain it;
inspect `io_errno` and continue retirement. `DONE` does not certify I/O success.
The adapter never frees staging or closes the borrowed range. Keep state, range,
staging and their mappings alive until retirement, then release them yourself.
No other consumer may read, seek or close that range while this binding owns it.

### Fiber integration exerciser

`examples/disc_fiber.c` uses the fork's public fiber API and direct-disc adapter.
It discovers a single ordinary ISO extent, queues it three times with fresh
predictor history, and runs bounded decode slices on a child
fiber, yields to its owner between slices, and sleeps the carrier only when
waiting with no other ready work. It reports actual decoded frame count, elapsed
time and decode throughput. It does not output audio or report a playback clock.
Supply your own admitted non-looping `/cd/test.adx`, or a path argument to the executable.
No media assets are included.

From `libADX`, `make example-object` compiles the exerciser; `make example-elf`
also requires this checkout's KOS libraries. Use exactly the fiber provider
selected by the fork build; never combine core and SH4ZAM fiber providers.
The predictor completes each FPU operation before yielding, retains normal ABI
mode and keeps no XMTRX state across transfers. An application with other services
that retain matrices must select `KFIBER_ATTACH_MATH_CONTEXT` when attaching its
runtime initially. Fibers share the carrier's MMU/TLS/errno; do not yield while
holding KOS mutexes or hardware transactions. The example's 16 KiB stack is a
provisioning choice, not a measured whole-call-chain stack bound.

These APIs compose with the fork's storage and cooperative scheduling without
changing global MMU/OIX policy. Audio port/Manatee integration and a complete
player/service lifecycle remain separate work. The host disc fixture tests
adversarial timing and lifetime contracts, not real DMA, fiber execution or audio.

## ACX and AFS indexed inputs

`<kosdec/adx_index.h>` provides original selected-member lookup over the same
`read_at` contract. Choose `KOSDEC_ADX_ACX` for the zero-header, big-endian layout
or `KOSDEC_ADX_AFS` for the `AFS\0`, little-endian layout. Each step requests at
most eight bytes from the provider. Positive short reads return `MORE`; waits
return `NEED_INPUT`. Caller decides when to retry. Header count/table arithmetic
is checked before addressing the selected eight-byte offset/length entry.

`DONE` publishes `lookup.member`. The selected nonempty range must fit within the
source and begin after the fixed entry table. `NOT_FOUND` reports an ordinal
outside the declared count; zero-length selected entries are `UNSUPPORTED`.
Terminal statuses, including cancellation, are sticky. Existing enum values are
unchanged. Lookup validates the selected range, not all other entries, payload
format, member overlaps or optional filename/timestamp metadata. Other variants
and appended partition services are not implemented.

Initialize a PCM stream after lookup reaches `DONE`:

```c
kosdec_adx_stream_init(&reader, kosdec_adx_view_read_at,
                      &lookup.member, lookup.member.bytes, repeat);
```

The view translates member-relative offsets, clamps each read to the member end
and 512 bytes, and returns EOF at the exact end without calling the parent.
Thus loop byte offsets remain relative to the selected ADX file; adjacent archive
data is never presented as its footer or compressed payload. The provider and
source must remain immutable/alive through all consumers. The validated member
may be copied by value; do not reinitialize its storage while a reader borrows it.

For an initialized KOS file binding, pass `file.stream.read_at`,
`file.stream.context` and `file.stream.input_bytes` into `kosdec_adx_index_init`.
Keep that file binding and its borrowed descriptor alive/exclusive throughout
lookup and member decoding. Reading those source fields does not transfer
ownership; the existing synchronous VFS restrictions still apply.

Container lookup and codec admission are independent. Successfully selecting an
entry does not imply it contains ADX or an admitted sample rate. The coefficient domain above is admitted; other domains remain unsupported.

## Ordered PCM sequences

`<kosdec/adx_sequence.h>` provides a caller-owned 16-entry queue over the same
`read_at` sources. Initialize, enqueue source descriptions, then explicitly start:

```c
struct kosdec_adx_sequence sequence;
struct kosdec_adx_source item = {read_at, context, file_bytes, user_tag};
kosdec_adx_sequence_init(&sequence, false);
/* Check these results before starting reads. */
enum kosdec_adx_status queued = kosdec_adx_sequence_enqueue(&sequence, &item);
enum kosdec_adx_status started = kosdec_adx_sequence_start(&sequence);
```

Each read performs at most one stream step and returns PCM from one entry, with
its user tag and first source-frame position. Sources may be resident, VFS,
direct-disc or indexed member views. Descriptions are copied; their contexts and
immutable data remain borrowed. Duplicate entries are supported. The current
entry occupies a queue slot; a full queue returns `NEED_OUTPUT` without mutation.
Additional entries can be enqueued while decoding, until the sequence is terminal.
An empty start returns `NEED_INPUT` without starting; a second start is invalid.

An entry completes only after its footer is validated through logical EOF. Then
it is removed, and repeating mode appends its description to the queue tail.
Disabling repeat lets all already queued entries finish. The queue advances at
decoded EOF, not when compressed data is read ahead, so these counters intentionally
do not duplicate CRI's input-service queue timing. `completed_entries`, `requeues`
and `returned_frames` are modulo-2^64 software counters, never playback clocks.
`count` includes the active entry. Keep providers alive through all duplicate and
repeated references, and retire any transport requests separately before freeing.

Each file starts with zero predictor history. Padding in its final compressed
group is never returned; no silence, overlap or crossfade is inserted. Embedded
loop metadata is rejected, as are changes from the first admitted sample rate or
channel count, before the offending entry emits PCM. This fixed-format policy
keeps the output contract stable; it is not a claim about every SDK format change.
Current codec profile restrictions still apply. Zero-frame files perform bounded
header/footer work and participate in entry counts without emitting samples.

Pause and zero output capacity suppress source calls. Cancellation/errors are
sticky, and cancellation does not close a provider or retire DMA. Header/footer
work and file transitions can return `MORE` with zero PCM; input waits return
`NEED_INPUT`. The consuming service must buffer and schedule enough work to hide
these intervals. Ordered PCM concatenation alone does not guarantee gapless
hardware playback. This service is not a CRI ABI shim or an audio player.

The sequence suite checks 288 combinations against independently derived rational
PCM vectors, including partial and zero-frame files, repetition, fragmented input,
waits and output capacities. It also tests queue-full/refill, duplicate tags,
start/pause/cancel behavior, malformed footers, early format rejection and ACX/AFS
composition. The disc suite separately runs a three-entry sequence through delayed
DMA/retirement modeling and 2 KiB staging. Hardware execution remains a distinct gate.

## Buffered PCM for a port or mixer

`<kosdec/adx_pipe.h>` binds an initialized stream OR a started sequence to
caller-owned PCM storage. The pipe supports exactly one producer and one
consumer, including separate threads or fibers. Storage capacity is a power of
two from 32 to 1048576 frames; reserve twice that many PCM16 samples to admit
stereo before the header is parsed. No heap, device, scheduler or transport
ownership is introduced.

```c
struct kosdec_adx_pipe pcm_pipe;
int16_t pcm_storage[2048]; /* 1024 frames, at most two channels */
/* reader is an initialized kosdec_adx_stream, owned by the producer. */
enum kosdec_adx_status status = kosdec_adx_pipe_init(
    &pcm_pipe, &reader, NULL, pcm_storage, 2048, 1024);
```

The producer calls `kosdec_adx_pipe_pump` once per service slice. Each pump does
at most one existing stream/sequence read, one 32-frame decode and two copies.
`NEED_OUTPUT` means wait for consumer progress; `NEED_INPUT` means wait for input
progress. `MORE` can represent metadata work with no PCM. A pump requires at least one free
frame and caps source output to min(free,32), including partial-block suffixes. Do not busy-spin
on a blocked source or full ring. Wake/scheduling policy belongs to the caller.
Synchronous file providers can block the carrier; asynchronous direct-disc input
uses its existing separate staging and request retirement.

The consumer uses `kosdec_adx_pipe_get_format` and `kosdec_adx_pipe_read`.
Read copies interleaved PCM into the consumer's own buffer, then releases ring
space. It neither calls the decoder nor inserts silence. Short reads expose
starvation explicitly; the eventual mixer decides how to handle an underrun.
Zero-capacity reads poll without consuming. Format publication is synchronized
and immutable; producer cursors and terminal state use lock-free 32-bit atomics.
Wrapped samples copy in two spans, including partial final groups and sequence
boundaries. Sequence tags are not carried through this PCM-only queue.

Existing stream/sequence controls remain producer-only between pumps. Producer
pause stops further decoding; buffered PCM remains readable, so presentation
pause must also stop consumer pulls. An atomic `kosdec_adx_pipe_request_cancel`
is safe from another thread; wake the producer so its next pump acknowledges it,
even when full or paused. An in-flight pump may finish first. Published PCM stays
readable after cancellation or error. Terminal status is distinct from a joined
producer, and `drained` only means an empty software queue after a terminal.
Join/stop both users and retire providers/downstream users before reclaiming or
reinitializing state. There is no device completion, DMA or sound-RAM guarantee.

The direct-disc example now pumps on a child fiber and consumes on main through
this pipe. This demonstrates the fork's cooperative scheduling without coupling
libADX to a fiber provider. Decoder math completes before every yield. It reports
decoded throughput and checksums consumed PCM; it does not submit audio.

The pipe suite compares 888 stream/sequence/loop cases with independent rational
PCM vectors, including input waits, small consumer reads, ring and uint32 cursor
wraps. Lifecycle tests cover full-ring cancellation and buffered data after error.
A separate producer/consumer stress run verifies 650000 frames and cancellation
while producing. Host thread tests do not establish target scheduling deadlines.

## Acknowledged producer controls

`<kosdec/adx_service.h>` adds a controller to the PCM pipe. Initialize a service
with one unpaused stream or started sequence, ring storage and capacity, using
`kosdec_adx_service_init`. It starts with production paused. The service owns
all producer/source calls; consume PCM through `service.pipe` as before.

One serialized controller posts copied commands: `PRODUCTION_PAUSE` (false to
start/resume), `RELEASE_LOOP` for a stream, and `SEQUENCE_REPEAT`/`ENQUEUE` for a
sequence. `post` returning `MORE` means accepted, not applied. Wake the producer
and collect exactly one receipt with `take` before posting again. A receipt of
`MORE` means applied; `NEED_OUTPUT` can mean the sequence is full, and
`UNSUPPORTED` means the operation does not apply to this source kind. Source
contexts remain borrowed through all accepted queue uses. Commands do no I/O.

The producer calls `service_step`: each call processes one command or performs
one bounded pipe pump. Pause gates production, including provider calls; queued
PCM remains readable. This is not audible pause. The existing fork Fiber Service
Executor supplies scheduling and latched wakes: park while paused/full, retry
input waits on an appropriate deadline, and yield after progress. The audio
example demonstrates this arrangement. A synchronous file provider can block
all services on its carrier; the direct-disc provider uses nonblocking steps.

`service_request_cancel` is safe from any thread and bypasses a full command
slot or paused/full PCM ring. Wake the producer afterward. EOF, error and cancel
close command admission; even a post racing terminal closure receives a result
without requiring the producer to run again. A previously completed receipt
retains its original result. Cancellation cannot interrupt a callback already
in progress and preserves published PCM. `get_state` observes producer status
and production pause only. There is no automatic restart or reset while live.

A receipt or terminal status does not join the producer. Stop callback wake
sources and join producer, consumer and controller before reset/free; separately
retire transport and downstream sound users. Service/pipe objects allocate no
memory and pull in no scheduler or audio APIs. The one-outstanding-command slot
provides payload ownership and acknowledgement, while the fork executor remains
the scheduler. This interface is original and does not claim CRI ABI equivalence.

Tests verify PCM continuity through controls, sequence capacity/repeat, loop
release, provider failure, cancellation of paused/full production, preserved
receipts at closure and concurrent controller/producer/consumer terminal races.

## Fork-native audio player

`<kosdec/adx_player.h>` supplies an optional audio owner over the producer service.
It lives in a separate archive object, so codec-only consumers do not link audio
or fibers. The application initializes the KOS stream system once with sufficient
channel and buffer limits. The player never resets or shuts down global audio.
It creates one private Fiber Service Executor for each bound source; sound operations
stay on the ordinary caller thread. Use exactly the fork's selected fiber provider.

Lend an initialized unpaused stream or started sequence, PCM ring, callback
scratch and aligned producer stack. The configuration and sizes are explicit:

```c
/* stream is initialized; sound stream system already initialized by the app.
   These buffers and player/source/provider state must remain alive. */
static _Alignas(32) int16_t pcm[32768], scratch[8193];
static _Alignas(32) unsigned char stack[16384];
static struct kosdec_adx_player player;
struct kosdec_adx_player_config config = {
    .pcm = pcm, .pcm_samples = 32768, .capacity = 16384,
    .scratch = scratch, .scratch_samples = 8193,
    .stack = stack, .stack_bytes = sizeof(stack), .sound_bytes = 16384,
    .transfer_timeout_ms = 1000, .prefill_timeout_ms = 10000
};
if(kosdec_adx_player_init(&player, &stream, NULL, &config) < 0)
    return -1; /* No player resources acquired on invalid init. */
/* Next call start, checking its result. On startup failure, preserve the error
   and stop/retry stop before releasing partial resources. After successful start,
   poll regularly on this same ordinary thread and inspect get_info. See the
   complete disc_audio.c example for ownership and error handling. */
```

`start` schedules decoding; `poll` waits for a complete sound-ring prefill or
validated source EOF, then allocates/starts sound. Prefill input waits have a
caller-specified timeout. Supply a power-of-two PCM capacity at least half the
per-channel sound byte count. The 16KiB stack is the current provision, not a
proven bound for arbitrary providers. Synchronous file providers may block their
producer carrier and delay stop/join; the direct-disc provider uses nonblocking
bounded steps. The player does not acquire, close or retire source providers.

`poll` handles output refill and can block in checked sound operations. Service
it more often than one sound-ring duration; missing live PCM becomes silence and
increments `starvations`. Counters distinguish copied source frames, requested
frames and callbacks. `SOURCE_END` means validated EOF and an empty CPU PCM ring;
it is **not audible playback completion**. Continue polling for application tail
policy, then stop explicitly. No estimated sound position is promoted to a clock.

`set_output` accepts per-channel linear volume and pan on KOS's0..255 scale,
before or during playback. Defaults are full volume, centered mono and separated
stereo. These differ from CRI attenuation/pan semantics. A live update error can
partially affect channels, so it latches failure and requires stop. Producer
`post`/`take` expose acknowledged pause/resume, repeat, enqueue and loop release;
production pause leaves already submitted sound playing. Terminal producer wake
rejection is harmless when queued PCM/receipts remain; live wake failure is not.

`set_rate` selects requested output frequency8000..48000Hz; zero follows the
source rate again. Set it before first playback or use the existing checked live
frequency update. It persists across track bindings. `info.sample_rate` remains
the decoded source rate; `info.output_rate` reports the requested output rate,
which hardware quantizes. Speed and pitch change together; no software resampling,
time stretching, semitone conversion or fade is supplied. Faster playback requires
more frequent polling and higher decode throughput. A partial channel-update error
requires stop, like volume/pan errors.

`cancel_source` interrupts a bound source before EOF, suppresses future callback
PCM/wakes, requests cancellation and joins the producer. After success it discards
queued CPU PCM and outstanding control receipts, entering IDLE if output exists
or unbound READY otherwise. Existing source errors survive cancellation. A failed
join leaves RELEASING, where poll keeps servicing silence; retry cancel_source or
perform final stop. Retain all borrowed state until join succeeds, and retire the
provider separately. Player, PCM ring, scratch and stack storage remain lent
through final player stop; only released source/provider storage may be reclaimed
after its own retirement. Already submitted sound may still play: this is not audible
pause or immediate silence. Bind the next source after that boundary, optionally
with a pending stream seek. No sound allocation/restart occurs when output exists.

`stop` closes new poll/control admission, requests producer cancellation, performs
checked sound stop, detaches callback/userdata and joins the executor. Failed
stop/join retains state and supports retry. `first_error` survives cleanup while
`stop_error` records the latest cleanup failure. Stop snapshots reported driver
errors before cleanup clears them; this is not an atomic history of errors that
can arrive after that snapshot. Missing-data/silence indications are counted
separately rather than treated as fatal errors. Do not free borrowed CPU storage
until stop succeeds and source-provider I/O independently retires.

The sound handle remains allocated and is returned as `info.retained_sound`
after successful stop. Keep ownership of it and the global sound resources;
this API does not destroy, restart, release or certify reuse of its sound RAM.
Consequently stopped players still consume the finite KOS stream/channel pool.
The output supports one start and repeated source bindings while it runs. It
does not support repeated device open/close cycles. Audio-owned final-fetch/reuse
work must resolve that separate limitation.
Only after successful stop/provider retirement may CPU player storage be freed,
keeping the retained sound handle separately. A stop timeout bounds sound waits,
not an arbitrary blocking provider or thread join.

To play another track, at `SOURCE_END` call `release_source`. It disables PCM
admission, joins the producer and enters `IDLE`; a failed join retains the source
and returns an error for retry or stop. Collect any needed command receipt first:
release discards outstanding receipts. After a successful release, retire the
provider before reclaiming or reinitializing it. The sound stream keeps running;
continue polling in IDLE to pad output with silence.

Initialize the next stream/sequence (optionally begin a stream seek), then call `bind_source` to start a fresh
producer using the same PCM ring, scratch and stack. Only the original output
rate and channel count are supported. Prefill continues polling silence until
format validation and adequate PCM are available; incompatible format fails with
`EPROTO` before any new PCM reaches output. A failed accepted bind requires stop;
an invalid argument leaves IDLE unchanged. `source_frames` resets on a valid bind,
while `copied_frames` and other counters remain lifetime totals; `sources_started`
counts sources admitted to the output. There is one sound allocation/start across
these bindings. Old submitted audio can still be audible after source release,
and join/provider work can interrupt polling. This is not a gapless or audible
completion contract; use the sequence API to queue consecutive sources in advance.

The current fork's aligned PCM16 assembly splitter reverses channels and adjacent
frames. Binding selects naturally16-bit-aligned, non32-byte-aligned scratch,
using the existing exact split path without changing PCM. The extra scratch
sample reserves room for that offset. Remove this workaround after the shared
Audio correction is accepted and available; do not compensate by permuting PCM.

## Audio player example

`make -C addons/libkosdec/libADX audio-example` builds
`.build/sh4/disc_audio.elf`. Supply your own admitted `/cd/test.adx`. The example
opens an explicit direct-disc source and exercises producer controls, output
controls, early source cancellation, bounded seek and output-rate changes. Supply
an admitted file at least three seconds long. It plays the first portion,
cancels and joins, retires the provider, seeks to source-rate+17frames, plays the
suffix at half rate, then replays the full file at normal rate. It reports each
source's copied frames, seek offset and requested output rate, then retains the
sound allocation/program. Post-source tail intervals are capture observation
policy, not a library completion or retirement rule. No driver, callback or
executor implementation is duplicated in the example.

Host player tests use the real decoder/service with a pthread producer and mock
sound calls. They check exact PCM, mono/stereo and sequences, short prefill,
production controls/starvation, source errors/timeouts, admission bounds, partial
startup/output failure, stop/join retries, callback detachment and sticky errors.
Additional tests combine cancellation at READY, prefill, live playback, production
pause and EOF with seek/rebind and frequency changes. Seek tests cover1584 exact
suffix cases and all66 fixture offsets through the PCM pipe, error/cancel/EOF
behavior and the per-step work bound.
Repeated mono/stereo stream/sequence bindings exceed the sound-stream pool limit
with one output allocation; format rejection and idle/prefill failure cases check
that unadmitted PCM never reaches the output.
The SH-4 build uses actual fork headers and runtime. Host models, emulator audio
capture and physical-device retirement remain distinct evidence levels.

## Arithmetic contract

The predictor uses float Q16 histories within each fixed logical channel group;
histories quantize on group return, independently of input fragmentation.
The single-pass codec never decodes padding; the resident reader uses full groups
as described above. General coefficient setup uses original certified Q32 decision
boundaries for all integer rates 1..48000 Hz and cutoffs 0..floor(rate/2).
The original 500 Hz pairs remain `{7334, -3283}` for 44100 Hz and
`{6569, -2634}` for 22050 Hz. See [coefficient certification](COEFFICIENTS.md)
for the exact quantization contract, proof and 46192-byte read-only table cost.
No approximate target transcendental helper is used.

The reviewed scalar math uses SH4ZAM definitions and a narrow native FMAC
primitive. A generic multiply-add expression is not substituted for one-round
FMAC. Strict compiler flags are enforced after caller optimization flags.
Target assumptions are the normal KOS single-precision profile (PR/FR/SZ/RM=0,
DN=1, traps masked). Control bits and XMTRX are preserved; FPUL and arithmetic
status flags are scratch. No cache/MMU mode is changed or assumed by the API.

Host primitive emulation supports regression tests only. The separate native
FMAC underflow discrepancy between the architectural reference and Flycast is
unresolved; fixture success is not full arithmetic or hardware certification.

## Remaining middleware work

See [the parity checklist](PARITY.md) for the full completion scope. Current port
asset requirements prioritize work without removing other baseline capabilities.

Additional formats and out-of-domain coefficient setup, broader ACX/AFS partition/metadata services,
SJ-compatible services, device stop/restart and allocation retirement, audible pause
and seamless playback, multi-stream scheduling, qualified playback timing,
CRI-unit volume/pan/pitch and fades, errors/recovery and host tools remain. The native player exercises output and software stop/join but does not provide
safe sound-memory reuse, audible completion or unlimited restart.
There are no success-returning placeholder APIs for these capabilities.

The synthetic suites cover fragmentation, truncated input, backpressure,
reset/cancellation, terminal behavior, mono/stereo admission, padded final groups,
variable metadata lengths, work budgets, loop records/offsets and declared-length bounds. The profile
suite checks PCM against independent exact-rational float vectors; regenerate
them with `python3 tests/generate_profiles.py`. This is not proof of proprietary
decoder equivalence. Target execution and hardware checks remain separate.

The memory suite checks 360 combinations against independent carried-history
rational vectors, plus lifecycle/error cases. Regenerate its vectors with
`python3 tests/generate_memory.py`. Whole-block history is supported by the
inspected SDK block/trap behavior; exact proprietary batching and waveform
equivalence have not been established.

The stream suite covers 576 combinations of loop endpoints, channels, input
fragmentation, output capacity, padded headers and intermittent waits, plus
truncation/errors and release at every position through the first endpoint.
PCM matches the independent rational loop vectors. File-adapter tests cover
short reads, interrupted/would-block reads, seek failures, incorrect seek positions,
size errors and caller-owned descriptor lifetime. These are host contract tests;
real filesystem/disc and audio execution remain separate validation gates.

The index suite checks 288 combinations of byte order, selected non-first member,
channels, loop endpoints, fragments, output capacities and waits against rational
PCM vectors. It also checks malformed directories, missing/empty members,
range overflow near UINT32_MAX, cancellation/provider failures and exact member
EOF with no access to adjacent archive bytes. Synthetic fixtures are original.

The rate suite adds 228 numerical/profile/loop cases at 22050 Hz, including both
channels and versions, partial groups, small output buffers, waits and invalid
rate/cutoff rejection. Regenerate setup certificates with
`python3 tests/generate_coefficients.py` (optional `--proof PATH` for detailed
rational bounds), and PCM vectors with `python3 tests/generate_rates.py`.
The certificate uses rational alternating-series and square-root intervals to
prove truncation results; it does not certify the SDK's transcendental functions
or establish full proprietary waveform equivalence.

The direct-disc suite adds 216 indexed mono/stereo loop cases through delayed I/O,
sector-crossing offsets, 2/4/32 KiB staging, padded headers/footers and capacities
of 1/7/32 frames. PCM is compared with independent rational loop vectors. It also
checks read-ahead reuse, EOF clipping, offset changes during requests, terminal
retirement delays, cancellation and injected seek/submission/status/destruction/
transfer failures. The mock is a host-only contract fixture, never installed.

The expanded-rate suite adds 2304 cases across 18 rate/cutoff profiles:
mono/stereo v3/v5 framing and exact PCM, loop/release through both readers,
boundary seeks with fragmented/waiting input, extreme history/clipping and
strided output sentinels. Generate its independent binary32 vectors with
`python3 tests/generate_expanded.py`. Player host tests check 16 initial-rate
configurations without issuing a live frequency update; they do not validate
physical pitch or resolve the separate stereo live-rate issue.

The setup suite checks 69288 transition-side cases, 1926 nearest-rational inputs,
zero/Nyquist endpoints and domain rejection at every integer rate through 48000.
Regenerate tables and special inputs with `python3 tests/generate_setup.py`.
The player model additionally verifies low-rate output admission and explicit
rate overrides; codec validity alone is not a physical output guarantee.
