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
calls. No calls allocate, block, yield or access hardware. An explicitly
configured PCM filter runs synchronously when output is returned.

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

Consumers must rebuild for the expanded metadata snapshot structure. PCM arithmetic is unchanged; optional filter configuration also enlarges decoder
and embedding reader/sequence state.

## Decoded PCM filter

Call `kosdec_adx_set_filter(&decoder, callback, context)` after initialization;
for a memory/stream/file source, pass its embedded decoder. For a sequence, use
`kosdec_adx_sequence_set_filter` so the setting survives every entry transition.
`NULL` detaches the filter. Initialization clears it; stream seeking preserves it.
Configuration returns `DONE`, or `BAD_ARGUMENT` for invalid/completed state, or
the decoder's sticky error. Source ownership and lifecycle rules still apply.

The callback receives `(context, channel, pcm, frames, stride)`. It may read/write
only `pcm[i * stride]` for `i < frames`; `stride` is the interleaved channel count.
There are 1..32 frames per invocation, one invocation per channel of a nonempty
output span. Caller read sizes can change batching. Predictor history is already
updated from unfiltered decoding, so an effect cannot feed back into the codec.
Loop padding and seek-discarded samples are excluded. Headers, footers, input
waits, pause and zero-output calls do not invoke it.

Configure on the serialized owner before binding a producer or after joining it.
Callback/context must outlive all producer calls. The callback must return
synchronously, preserve FPU control modes, and must not block, yield, reenter or
mutate its source, or retain the borrowed PCM pointer. In a pipe/service/player,
it runs on the producer before publication; the audio consumer never calls it.
Its execution time is part of the producer's budget.

The native signature uses PCM frames and interleaved stride; CRI's callback uses
per-channel byte lengths. This is a native behavior equivalent, not a CRI ABI
wrapper. Audible-time amplitude extraction remains a separate unfinished feature.
Rebuild consumers for the enlarged decoder and embedding reader/sequence state.

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
`DONE`. There are no allocations, scheduling or device operations. An optional
PCM filter runs for returned frames.

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

## Native decoder verification

With the fork's toolchain configured and its KOS archive built, run
`gmake native-test-elf` from this directory. Boot `.build/sh4/adx_native_test.elf`
on a Dreamcast or emulator. The serial console prints `ADX-NATIVE PASS` after
the coefficient, PCM, clipping, loop, SFA and ordered joint checks finish. The
fixture uses synthetic vectors and the actual SH-4 archive. Audio output and
sound-memory retirement have separate validation.

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
and full file-handle services remain incomplete; resident directories and sparse
partition overlays are described below.

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

For resident data, initialize an immutable memory source and select the
whole file, a range, or an ACX/AFS member. Borrow its descriptor, then
pass that exact loan to `kosdec_adx_source_init_memory_joint`. The helper
seals a read-only joint over the selected bounds without copying the
compressed bytes. Initialize a joint reader and use the normal player
start or rebind. Join the player and close the reader and joint before
returning the descriptor.

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
stereo. This legacy API retains KOS units. A live update error can
partially affect channels, so it latches failure and requires stop. Producer
`post`/`take` expose acknowledged pause/resume, repeat, enqueue and loop release;
production pause leaves already submitted sound playing. Terminal producer wake
rejection is harmless when queued PCM/receipts remain; live wake failure is not.

`set_pan` and `get_pan` add signed positions -15..15 and
`KOSDEC_ADX_PAN_AUTO` (-128), independently for each channel. AUTO is the default:
mono centers channel0; stereo separates left/right. An explicit setting on one
channel no longer suppresses AUTO on the other. Settings persist across source
release/rebind and apply before output starts or through checked live pan updates.
Channel1 may be prepared before format discovery and is ignored for mono output.
Getters return the last successful software setting, including AUTO, even after
failure/stop; they are not device readback. Invalid channel/value/state is EINVAL
without mutation, rather than reproducing unchecked SDK indexing or clamping.
Legacy `set_output` still emits the exact supplied KOS bytes and clears AUTO for
both channels; `get_pan` then reports their equivalent signed positions (zero
attenuation in either direction is center). Live AUTO resolves immediately from
the admitted format rather than forwarding the sentinel to a lower-level clamp.
No stereo-atomic or hardware qualification follows from the checked call.
Rebuild consumers for the expanded implementation-owned player state.

`kosdec_adx_player_set_filter_settings` stores the raw Dreamcast cutoff and
resonance values and `kosdec_adx_player_get_filter_settings` reads them back.
They persist across source rebinds. The SDK's Manatee route leaves playback
unchanged by this setting. The producer PCM callback remains separate.

### Ordinary stereo live transpose

Build a separate archive with `ADX_ORDINARY_STEREO=1` against the fork's
ordinary PCM16 backend. Bind a READY compatibility title to a direct
unlinked stereo ADX stream, then call
`kosdec_adx_compat_player_enable_stereo_transpose` before starting.
Use 8192–65504 sound frames per channel in multiples of 32 and source
and initial output rates in 8000–48000 Hz. Initialize the KOS stream pool for
two channels. Keep the player and its provider storage through checked
stop and producer shutdown. Rebuild consumers with the matching headers.

Use `kosdec_adx_compat_player_set_live_transpose` to apply the cached
semitone/cents request through separately acknowledged slot updates.
Checked ordinary mono and stereo output accepts exact live pitch
representatives from 1889 through 186736 Hz. Rates above 48000 Hz require
at least 32768 frames per channel. Above 88200 Hz use a 65504-frame ring,
at least 65536 queue frames, 131072 PCM samples of queue storage, and
65505 scratch samples. Service and production must keep up with the
output rate. Unsupported requests preserve the admitted rate.
`EAGAIN` before an UPDATE is posted preserves playback and the last
admitted rate. Service the player and explicitly retry.
On a partial update or refill failure, preserve ownership and complete
checked stop, retrying it after a timeout. Keep the played reservation.
Complete stop before shutting down sound or resetting G2. The private
output owns its DMA through callback retirement.

Live transpose can also apply after decoding ends while the last
qualified slot-0 cursor still has real source PCM. A right-only remainder
or padded silence does not keep the title eligible. The compatibility
clock uses slot 0, counts decoded source frames only, and divides by
nominal source Hz. Padding and changed output pitch do not change the
denominator. The optional retained clock,
live pan/gain/routing and native frequency overrides use other output
profiles. The build and player opt-ins leave the existing mono and
default paths available.

### Ordinary mono clock and live transpose

Use the fork's ordinary PCM16 build and bind a READY title to one finite,
direct mono ADX source. Enable `kosdec_adx_compat_player_enable_mono_clock`
before starting. Ring geometry and live rate admission follow the checked
ordinary profile above. A live change requires acknowledgement from the
one output slot. Time uses that slot's
qualified position and the nominal source rate. PCM padding does not count
toward source time. The optional logical PLAYEND gate uses actual source
drain and checked stop while keeping the played allocation. Build
`ordinary-mono-clock-test-elf` with `ADX_ORDINARY_STEREO=1` against the
matching backend. Generate the mono fixture with
`tests/generate_flycast_pause.py proof 81920 1`. For the wider mono and
stereo tests build `ordinary-wide-transpose-test-elf`. Generate the
44,100 Hz `proof` track with 393216 frames and the 8,000 Hz `fresh` track
with 49152 frames, using the channel count for the selected ELF.
The high-rate stereo interpreter run starved with 65,536 producer queue
frames and passed with 262,144; both runs used the same 65,504-frame
sound ring per channel. Ring admission alone does not guarantee that the
decoder will keep up.

### Flycast active pause

Build with `ADX_ORDINARY_STEREO=1 ADX_FLYCAST_ACTIVE_PAUSE=1` against
the matching build-gated Audio backend. Bind a READY title with one finite,
direct mono or stereo ADX source at 22,050 to 44,100 Hz. The startup output
rate must match the source rate. Enable
its checked ordinary output and then
`kosdec_adx_compat_player_enable_flycast_pause` before start. This profile
uses a 16,384-frame/channel sound ring. It requires an owner and a
producer that can supply a dense continuation. For compatibility NTSC or
PAL time, enable the collection's VBlank clock before binding and then
select the desired time mode. While paused, VBlank delivery does not advance
the count. Resume re-anchors to elapsed source time and the current tick. Rebuild with
matching headers.

During PLAYING, the compatibility live transpose call accepts an output
rate within the same range. Public sample time still uses the nominal source
rate. If the update returns `EAGAIN` before posting, service the player and
explicitly retry. A pending or held cut rejects a new live request. Resume
starts the new bank at phase zero with the last admitted pitch.

Call the normal compatibility pause and discard APIs. A pending cut must
finish before discard or resume. A cut can occur after earlier wraps or
across the ring boundary while the live cursor and committed window
remain qualified. A pause timeout can leave a selected cut pending, even before
the channels are armed: retry pause or complete checked stop. In contrast,
`EAGAIN` before cut selection permits ordinary service and a
new pause attempt. Resume uses the same reservation and inserts 64 silent
frames in the PCM ring before the first surviving source frame. At a
fractional device pitch, the mixer interpolates across that boundary. A finished source
may resume with a short real tail followed by silence. Use the logical
PLAYEND opt-in before start to report completion after the tail and checked
stop. A live producer shortage returns EAGAIN with the previous cut intact.
Preroll, padding and discarded frames do not accrue elapsed source time. A hard resume failure
requires checked stop. Retain the played reservation after stop. One pause-enabled title owns
the two banks for the sound runtime (64 KiB total for mono, 128 KiB for
stereo). A subsequent fresh ordinary title uses its own allocation.

The synthetic regression builds with `flycast-pause-test-elf`. Run
`tests/generate_flycast_pause.py` with Python and each of the names `proof`
and `fresh`. Mount the generated `disc` tree alongside the test ELF. It
exercises five cycles, a withheld cut result retried on the same lease,
a checked stop and an independent fresh title. The
`flycast-pause-vsync-test-elf` target also exercises NTSC and PAL time
with explicit owner-delivered VBlank events. Build
`flycast-pause-late-test-elf` and generate each track with 393216 frames
to exercise post-wrap cuts and cuts that cross the ring boundary. For
finite tails, build
`flycast-pause-tail-test-elf`. Use `proof 22016` with the generator for
case 0 and `proof 26048` for cases 1–3. The
`flycast-pause-mono-test-elf` target uses mono tracks generated with
`proof 393216 1` and `fresh 393216 1` for sample and PAL time. The
`flycast-pause-32k-test-elf` target builds mono and stereo post-wrap
fixtures at 32,000 Hz. Generate both tracks with the matching channel
count, for example `proof 393216 1 32000` and `fresh 393216 1 32000`.
The `flycast-pause-live-transpose-test-elf` target uses 44,100 Hz tracks
of the same lengths. It requests −12 semitones before the first pause and
checks the admitted pitch through each resume and terminal source drain.

### Retained output clock

Build with `ADX_RETAINED_CLOCK=1` against the fork's private retained-PCM
backend, using a separate build directory. Before the first start, call
`kosdec_adx_player_enable_retained_clock` with caller-owned offset storage
of one `uint32_t` per PCM ring frame. This profile requires default gain and
pan and a fixed PCM16 output rate. The retained ring must have 1024 to 32768
frames per channel. Keep the offset and player storage alive through
successful stop.

The retained clock has single-player coverage. Concurrent retained
players are experimental. The two-player clock-only Flycast fixture
returns `EPIPE` during group polling.

The player's source generation identifies one binding. Loops and records in
that binding can reset the media offset. Source and silence spans stay
distinct, including source samples with value zero. A fill that cannot fit
the backend's 16 spans fails before consuming input. Up to 16 sources can
be admitted on a lease. Admission of another source then fails with
`EOVERFLOW`. Cancellation leaves a partially delivered generation unsealed.

Query `kosdec_adx_player_get_source_output` with the source generation.
Use the clock only while `valid` is true. `source_output_end` means the
qualified channel cursor traversed all delivered source spans. CPU
`SOURCE_END`, audible end and final sample-fetch retirement remain distinct.
The checked stop retains the played Audio allocation. Audible pause and
discard still require separate device support.

For a playback display, `kosdec_adx_player_get_audio_time` returns the
qualified cumulative source frames and centiseconds for a nonzero generation.
Silence inserted by the player does not advance the count, and a loop does not
rewind it. Centiseconds use the decoded sample rate, including when an output
rate override changes speed and pitch. `kosdec_adx_player_set_time_offset`
applies a signed display offset in source frames across bindings. A stale or
unavailable clock returns an error and leaves the result untouched. This
implements the sample clock. Rebuild player consumers with the matching
header and archive.

For video synchronization, call `kosdec_adx_player_enable_vsync_clock` after
retained-clock setup and before first start. Supply a callback that reads
an existing counter with one tick per display VBlank, with NTSC (59.94 Hz)
or PAL (50 Hz) selected to match the display. The player reads it on its
owning thread and anchors the count when a new generation first submits
source PCM. Prepared-start waits and polls with no refill leave it
unanchored. `kosdec_adx_player_get_vsync_time` returns a signed count and
frequency for the current generation. One VBlank advances the estimate
by 100 units. Over 60 ms of difference from the qualified audio clock
refreshes the anchor for the next query. The display offset adds the same
signed setting in VSync count units and contributes in source frames to
the drift comparison, matching the SDK's unit handling.

The VSync query covers active playback and drain. Invalid observations and
canceled sources leave the result unchanged. At source-output end it reports
`ENOTSUP` pending Audio qualification for terminal timing, pause and discard.
The sample-clock query remains available under its own contract.

Use `kosdec_adx_player_set_time_mode` to select source, NTSC or PAL for
`kosdec_adx_player_get_time` and `kosdec_adx_player_get_time_real`. The
first returns the count and its frequency. The second reads time
independently and returns signed centiseconds truncated toward zero.
Source mode is the default. The owner can switch cadence during
playback and must keep the VBlank counter continuous across display
changes. A switch keeps the accumulated VSync count and tick anchor.
The explicit audio and VSync queries retain their own units. Rebuild
player consumers with the matching header and archive.

Initial retained attenuation is opt-in for mono and stereo. Build with
`ADX_RETAINED_CLOCK=1 ADX_RETAINED_START_TL=1` and a fork that exposes
`snd_retained_start_tl.h` privately. On a READY compatibility player,
select the retained clock, enable initial attenuation with
`kosdec_adx_compat_player_enable_initial_attenuation`, and set the
requested volume before start. DECINFO stages the signed cached value.
A later request before the first lease opens stages its full signed
argument. The lease applies the selected stock TL before key-on. See
[native middleware](NATIVE_MIDDLEWARE.md) for the startup and error contract.

Live retained attenuation requires `ADX_RETAINED_CLOCK=1`,
`ADX_RETAINED_GAIN=1` and a supporting backend and firmware.
Both Flycast CPU modes verified repeated gain changes on one 44.1 kHz
mono ADX player with an 8,192-frame sound ring and 1 ms service sleep.
Stereo remains experimental. The combined stereo fixture returns
`EPIPE` during polling after the first request was applied. The
default archive leaves live retained gain disabled.
`kosdec_adx_player_set_attenuation` takes signed tenths of a dB and returns
a request ID on local admission. The effective value clamps to -999 through
0 and selects the stock 2.00J PCM route's zero-fade response after playback
starts. The requested getter returns the signed low 16 bits of the accepted
value before clamping. The setting applies to the active lease channels across
source bindings.

Service the player through its normal poll or compatibility service entry
and query the exact ID with `kosdec_adx_player_get_attenuation_status`.
Mono uses the left applied bit and leaves the right status fields zero.
Stereo acknowledgement requires both applied bits and carries no
same-sample guarantee. An unknown channel can still have a historical
acknowledged value, and poll can succeed with a pending retry. A terminal
gain error requires checked stop. The source clock rejects observations
while gain receipts are being refreshed. Missing backend or firmware
support returns `ENOTSUP`. Fades and guaranteed acoustic mute require
separate support.

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
as described above. Versions 3 and 5 through 44100 Hz use the rounded Dreamcast
2.00J setup model. Higher-rate extensions use the certified
exact-real setup. All admit integer cutoffs 0..floor(rate/2) within the
supported sample-rate range.
The original 500 Hz pairs remain `{7334, -3283}` for 44100 Hz and
`{6569, -2634}` for 22050 Hz. See [coefficient certification](COEFFICIENTS.md)
for the quantization contracts, tables and validation.

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
CRI-unit volume/pitch and fades, errors/recovery and host tools remain; signed
pan/AUTO setters and getters now have native equivalents. The native player exercises output and software stop/join but does not provide
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
inspected SDK block/trap behavior. See the joint job mode below for tested
2.00J batch boundaries and PCM.

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

## Byte stream joints

`kosdec/adx_joint.h` supplies caller-owned memory, ring and queue chunk transports.
Memory begins readable, offers no writable space, and reset replays the same
bytes. Ring begins writable and optionally mirrors its prefix after the ring
allocation so chunks can cross the wrap boundary without a consumer copy.
This is compressed-byte storage; it is separate from the interleaved PCM pipe.

The serialized owner gets a FREE chunk, fills it, splits off the used prefix,
puts that prefix, and ungets the unused suffix. A consumer does the same on
DATA, reading rather than writing. Put publishes to the opposite line; memory
consumption discards bytes. An explicit next-DATA call admits a second ordered
DATA loan. Other lines allow one loan each. Tokens and
span bounds reject stale, duplicate, foreign and out-of-order returns. Reset and
close reject outstanding borrows. Caller joins external users before close;
software retirement does not retire DMA or a transport provider.

Query exposes total/contiguous/borrowed byte counts and native transport kind.
An empty DATA chunk means starvation, not EOF. Error callbacks are synchronous,
non-reentrant notifications; ordinary errors do not mutate state. All backing
storage, state and output descriptors must be disjoint (split alone permits its
input to alias either distinct output). The API performs no allocation, IRQ
masking, cache maintenance, FPU work or scheduler operations. It can be driven
by the existing producer fiber, with yields outside borrowed-buffer operations.

The native contract deliberately uses typed status/kind instead of CRI error
numbers/UUIDs and does not support arbitrary concurrent or multiple same-line
borrows. Full SJ conformance remains open;
this facility alone does not close the middleware milestone.

The generic queue uses caller-supplied typed nodes and four FIFO lines. Seed
admits nonoverlapping ranges; explicit move selects a destination line. Separate
mode preserves chunk boundaries and returns the entire head only when the request
can hold it. Join mode combines adjacent spans and admits partial head reads.
Every pair of adjacent ranges that may join must belong to the same original
backing allocation. It never copies payload bytes. Query exposes queued chunk
counts, head lengths and spare-node counts. Seed overlap checks scan the nodes;
normal queue operations use constant-time list endpoints.

Each outstanding borrow reserves a node, so its full rollback remains possible
when the pool fills. Partial reads or returns may need another node and return
LIMIT unchanged if none is available. Reserved nodes are excluded from spare
counts; this deliberately differs from the SDK's unchecked pool accounting.
Reset forgets all seeded ranges and restores the node pool, only with no borrows.
Neither reset nor close frees caller buffers. Auxiliary lines have caller-defined
read/write roles; FREE/DATA retain the usual producer/consumer roles. Joint state
and query structures grew with queue support: rebuild all users of this interface.

`kosdec/adx_joint_reader.h` connects DATA chunks to the existing incremental float
decoder. It supports forward-only PCM output from memory, ring and either queue
mode, including optional PCM filters. Each read performs at most one decoder
step (32 PCM frames or 512 metadata bytes). No new codec arithmetic or transport
worker is introduced. Queue reads borrow a whole head, requiring no spare node;
a large head stays borrowed across multiple bounded decode calls. Memory/ring
admission is capped at 512 bytes. Returned PCM is interleaved and needs a full
logical group of output space, as documented by the underlying decoder.

A producer calls `kosdec_adx_joint_finish` after joining writers and completing
all input publication. This seals admission until reset. An empty unsealed joint
means starvation; DONE requires the actual validated footer and sealed EOF.
Cancel stops decoding but retains its input borrow until successful reader close.
Close abandons any unread tail of the held chunk, returns it to FREE (or discards
it for a memory joint), and never resets/frees the joint or joins an external
provider. Restarting a file needs a fresh header, not the abandoned cursor.

### 2.00J decode jobs

Call `kosdec_adx_joint_reader_enable_jobs` before starting the producer to use
the stock batching rules on memory, mirrored-ring or queued input. Supply a separate
PCM work buffer with two samples per frame of the configured job limit. The
player and service use this buffer for each job; the PCM pipe keeps its existing
consumer buffer. Configure 31 frames of output wrap space for arbitrary loop
positions. Mirrored compressed rings need at least 35 extra bytes to expose
a complete stereo group across a wrap. A queue job reads one contiguous head.
Publish complete groups, or append adjacent storage in JOIN mode to complete
the head. A short head waits while unsealed and reports truncation after seal.
Provision a spare queue node for a partial return that cannot merge with FREE.

Jobs return unread DATA to the joint and retire only the consumed prefix. The
published PCM count can differ from the source advance. For example, a
63-frame stereo trap with a 63-frame job budget can consume 36 bytes and
publish 31 frames while retaining the full group's predictor history. The
pipe tags each published frame with its logical position before any rewind.
Filters still receive chunks of at most 32 frames.

A version-3 memory loop can give its sample position and whole-group
compressed offset independently. The repeat flag starts enabled when
requested during setup. During playback, post `KOSDEC_ADX_RELEASE_LOOP` to
the service to finish the current loop interval and continue from its
compressed and sample positions. Job playback ends at the start of a
group-aligned end marker or after the reported sample limit. The strict reader
and jobs paired with a live SFA writer validate the footer and sealed EOF.
For live SFA, set the exact final frame count before publishing the last
partial group. Ring-source repeat uses a sector provider, which owns input
publication and cannot share a reader with a live SFA writer.

Attach a fixed-extent provider with `kosdec_adx_joint_reader_set_sector_source`
to schedule streamed-file jobs. The reader publishes completed requests in whole
2048-byte sectors and can prefetch while PCM output is full. Version-3 loop
start offsets are floored to sectors. The effective start must point to the
first compressed group of the loop. The logical interval must use all
encoded groups before the loop's sector padding. At the sector-end callback the provider either
seeks to the loop sector or releases the sample trap. The decoder discards
sector padding when it reaches a live sample trap and plays queued data in
order. A release after a prefetched seek can therefore allow another rewind.
The provider is borrowed until checked close. After joining the reader, retire
any separate file or disc request resources before reclaiming them.

Queue `detach` returns a borrowed prefix to caller ownership without enqueuing it.
This lets a producer reclaim FREE chunks for reuse elsewhere, or explicitly pass
storage between joints. It never frees payload; detached bytes leave capacity
and overlap accounting. Fixed memory/ring storage cannot be detached. All joint
and reader operations share the serialized owner; fiber scheduling belongs outside
these operations. This reader also binds through the native pipe/service/player alternatives below.

`pipe_init_joint`, `service_init_joint`, `player_init_joint` and
`player_bind_joint` bind a joint reader as a producer source. The existing player
fiber exclusively owns both reader and joint while bound. For live input, attach
an optional `joint_reader_set_feed` callback before binding. It runs at most once
per read on that producer, supplies bytes through joint APIs, and reports MORE,
WAIT, END or ERROR. END performs checked sealing; ERROR becomes sticky IO_ERROR.
WAIT can coexist with already queued readable DATA. No feed runs in the audio
consumer callback, while production is paused/cancelled/terminal, with zero output
space, or while the PCM pipe is full. NEED_END uses the normal input-wait deadline.

External transport/IRQ callbacks may fill their own staging and wake the producer;
they must not mutate the joint concurrently. Use bounded nonblocking feed steps
with a shared executor. Synchronous blocking contributes to the producer budget.
After checked player release/cancel/stop joins the producer, close the reader and
separately retire feed-provider requests/staging before reclaiming resources.
Cancel never invents provider retirement. A same-format joint can rebind without
another sound allocation/start; mismatch fails before new PCM output. Production
pause/resume and cancel work; sequence enqueue/repeat and embedded-loop release
are UNSUPPORTED for a forward-only joint source. Sound RAM remains retained under
the existing player contract. These are source/host/link facilities, not new
qualification of audible completion, final sample fetch or physical performance.

## Loop control and count

Select embedded looping when initializing a memory/stream reader. While playing,
only release is supported; re-enabling belongs before a new start. This follows
the baseline middleware manual's restriction and is not a missing live-enable
feature. Release preserves the remainder of the current traversal and plays out
to the file's validated end. Full format/history coverage remains separately tracked.

`kosdec_adx_pipe_get_loop_count` provides an atomic source-rewind count modulo
2^32, also exposed as `player_info.decoded_loops`. It is zero initially and for
sequence/joint sources; playlist repeats are a separate quantity. The producer
publishes after each step, including a rewind followed by provider WAIT/error.
Release, pause, cancel and terminal status preserve the count; init/rebind clears
it. The snapshot is independent of other PCM/status fields. Decode read-ahead can
lead sound output: this is not an audible-loop clock or sample-fetch certificate.
The baseline SDK likewise increments its loop count from a decoder trap, though
exact service/prefetch timing is not asserted identical. Rebuild callers for the
new pipe and player-info fields.

## PCM amplitude analysis

`adx_amplitude.h` provides caller-owned peak records for complete, nonoverlapping
PCM windows. Each record contains absolute peak0..32768, source-frame start,
window length and sample rate. Configure window size in frames;4410 frames gives
100ms at44100Hz. This is a peak detector, not RMS or a spectral transform.

Allocate typed records using `kosdec_adx_amplitude_work_size`, initialize one or
two channels, then either feed strided PCM or attach `kosdec_adx_amplitude_filter`
to the existing source filter slot before producer binding. Manual feed consumes
at most512 frames and backpressures when its queue fills. The filter adapter
cannot stall decoding: overflow latches a queryable fault and stops analysis,
while playback PCM remains unchanged. Drain/provision the record queues to avoid
that condition; reinitialize only after producer join and consumer quiescence.
A custom filter can call the adapter before or after its own PCM modifications.

One producer owns feed/filter/finish, and one consumer per channel owns read/select.
Records use the same lock-free32-bit cursor discipline as the PCM pipe. `finish`
discards incomplete windows and seals input; it does not detach/join a source.
`status` reports adapter faults even when older records are still available.
`read` consumes a record; `select` retains the first available start at/after a
caller-supplied source-frame position, or the latest available record. Selection
is forward-only as older records are discarded. With the retained clock enabled,
query `kosdec_adx_player_get_audio_time` for the analyzer's source generation
and pass `max(time.frames, 0)` to `select` only on success. Give each generation
its own analyzer and feed it from the first PCM emitted by the bound source.
This keeps record positions and the source clock on the same timeline across
loop rewinds. Keep selecting during playback or provision the queue for the
whole recording. An offset change can move the request backward, but discarded
records stay discarded. The native record/storage API uses caller-owned records.

The exact baseline format boundary and remaining container work are recorded in
[FORMATS.md](FORMATS.md); passing the SDK sample corpus is not full parity.

## Compressed silence insertion

`kosdec_adx_joint_insert_silence` appends zero-filled ADX groups to a ring input
joint, rounding requested frames down to32 and reporting actual inserted frames.
It handles ring wrap and mirrored storage without partially publishing a group,
writes at most512 compressed bytes per call, and returns zero when no complete
group fits. An outstanding DATA borrow may remain; FREE borrows block insertion.
Use it only on the serialized input producer (including a bound joint feed), at
compressed group boundaries after the header and before the endcode. The caller
must account for inserted frames in the header and any loop byte offsets.

The name follows the baseline middleware operation. Zero residual groups preserve
predictor histories: decoded output can have a transient after nonzero PCM.
This is not a predictor reset or a PCM-zeroing filter. It does not provide SFD
unknown-length/header coordination. Output-side sample discard is a separate Audio
contract requiring actual queued-frame counts and playback-clock adjustment;
source seek and producer-buffer discard do not establish that behavior.

## Resident AFS partitions

`adx_partition.h` loads an entire original AFS directory into caller-owned typed
ranges. Each step makes at most one source call of512 bytes and validates at most
64 entries. Short reads and temporary WAIT preserve progress; errors and cancel
are sticky. Member access is withheld until every entry is validated. Empty
members and overlapping immutable ranges are retained, while nonempty ranges must
follow the directory and fit the source. Optional filenames are not interpreted;
sparse overlay directories use the same validated offset/length table.

Use `partition_init` with the same `read_at` contract as streams, then step until
DONE. A zero-capacity first pass returns NEED_OUTPUT after a valid nonempty header;
`partition_get_info` reports the required typed work size. Close that sizing pass
and initialize with enough storage. Empty directories need no entry storage.
Caller-owned memory remains borrowed through successful partition close.

The256-slot registry binds completed directories to IDs. Aliases and multiple
registries are counted; closing a registered partition returns BUSY unchanged.
Replacement/unbind/registry-close releases metadata references. All operations
are serialized by the caller; no globals, file handles or implicit worker exist.
Registry lookup borrows metadata until its next mutation. `partition_open_view`
copies a selected range into an independent view with an explicit source callback
for the same immutable archive. The copied view survives directory close, but its
source/provider must remain alive through all payload consumers. Use that view
with `kosdec_adx_stream_init` and the existing service/player path.

`kosdec_adx_view_init` also constructs checked byte-range views directly. Empty
views report EOF without a provider call; nonempty reads remain capped at512 bytes
and their selected extent. View construction validates bounds, not payloads.
Native typed byte ranges replace the SDK's compact sector work layout. Directory
source callbacks do not implicitly acquire or retire VFS descriptors, disc
requests or DMA staging.

### Sparse partition overlays

Initialize a caller-owned `partition_overlay` link, then `partition_push` a
completed sparse directory above a bound base. Every nonzero-offset entry overrides
the same file ID in older layers; zero offsets fall through. A nonzero offset with
zero length explicitly overrides an empty member. The newest directory count
limits visible IDs, including when it is smaller than an older directory. A hole
at a newly added ID with no older member returns NOT_FOUND.

`partition_lookup` returns the newest directory for count/status queries.
`partition_resolve` walks the overlay chain and returns the selected directory
and copied byte range. Build a view using that directory's archive provider;
using the base archive's callback for an overlay range would read the wrong data.
The borrowed directory identity lasts until the next registry mutation; a copied
view retains no directory/workspace reference but still borrows its source.

A link belongs to one registry/ID at a time. Pop removes only the newest link;
unbind, different-base replacement and registry close detach all associated links.
Same-base binding preserves overlays. Detached links can be reused without reinit.
Each link counts a directory reference, including repeated use of one directory
through separate links; partition close stays BUSY until every reference retires.
Operations are externally serialized, allocate nothing and perform no I/O.
Resolution costs O(number of layers); the caller controls chain depth. Rebuild
consumers because registry state now includes overlay heads. Filename composition,
sector read/seek/reload services and full file-handle lifecycle remain open.

## Bounded sector requests

`adx_request.h` supplies caller-owned, caller-serviced reads over the existing
immutable `read_at` provider contract. Initialize with a byte extent; sector size
is2048 and file sector count rounds up. Start accepts a sector count and output
capacity, clips admission to EOF, checks the full capacity before any mutation,
and returns READING or immediate READEND for zero/EOF. There is no hidden worker,
allocation or global handle pool. Multiple independent requests are supported;
serialize access when their providers share a descriptor or staging object.

Each step makes one provider call of at most512 bytes, or writes at most512 bytes
of final-sector zero padding. WAIT preserves state for later scheduling. The
provider must release the destination before returning, including on WAIT/error;
any asynchronous staging and backend retirement remain its responsibility.
Use an appropriate owner thread/fiber for callbacks that can block. This API is
a copied byte destination and permits byte alignment; it is not a DMA endpoint.

Info distinguishes admitted sectors, completed sectors, actual source bytes and
written output bytes. Cursor stays at request start while reading; completion or
error advances it once by whole completed sectors. Stop releases the output borrow
and leaves that cursor unchanged, so restarting a stopped read rereads from its
starting sector. A valid SET/CUR/END seek stops then clamps to the file extent,
including extreme signed offsets. Invalid controls preserve an active request.
Close invalidates the request but does not close a descriptor, cancel backend
staging or establish DMA retirement. Preserve/retire the provider separately.

The final partial sector is zero-padded instead of exposing bytes after the
selected extent. On partial stop/error only complete sectors are committed;
remaining written bytes are diagnostic progress. The buffer is borrowed until
completion/error/stop/seek/close. No consumer may race with partial writes.
The new request layer supplies sector read/seek/progress equivalents; global service policy, directory resolution and reload limits remain open.
Owned filename sources are described below.

### Request destinations in stream joints

`request_start_joint` uses a ring or queue as its output. It publishes exact source
bytes, omitting final-sector padding so strict ADX footer validation sees the real
end of the selected file. A completed short final sector still counts toward the
requested sector range. Ring/JOIN chunks hold at most512 bytes; SEPARATE queues
retain and fill a whole head through multiple bounded steps before publishing it.
A final partially filled head returns its unused suffix before publishing its
prefix. Queue node pressure returns NEED_OUTPUT without abandoning ownership;
provision spare nodes for partial SEPARATE tails, drain/recycle DATA, or stop.

The request can retain one FREE borrow across WAIT/partial reads. Serialize all
joint access; a DATA consumer may run between steps, while another FREE producer,
reset, close or seal must wait until request retirement. Stop/seek/close return
unpublished storage. Already published DATA stays queued; replay requires the
caller to drain/reset it as appropriate. No implicit seal is performed, allowing
several requests to append before an explicit final `joint_finish`.

`source_bytes` can include data fetched into an unpublished span; `output_bytes`
counts only published data. Normal terminal reads release the held span. A caller
violation that prevents returning a borrow leaves the request active and reports
IO_ERROR; successful stop/close is required before storage reuse. Provider-side
staging/DMA remains independently owned and must still be retired by its owner.
The request structure grows; rebuild consumers. Readonly memory joints cannot be
destinations, and native byte-sized buffers replace the SDK sector-size restriction.

## PCM WAV reader

`adx_wave.h` implements a separate bounded RIFF/WAVE PCM reader: tag1, unsigned
8-bit, signed little-endian16-bit or CRI packed4-bit ADPCM, mono/stereo. Each read makes at most one
provider call of512 bytes and emits at most32 interleaved signed16 frames. U8 is
converted exactly as `(x - 128) * 256`; PCM16 loads are byte-safe. These are exact
format conversions, with no FPU transform or SH4ZAM substitution needed.

Header processing walks RIFF chunk boundaries and word padding, skips unknown
chunks, requires one valid fmt before data, and checks rate/block alignment,
byte rate, whole-frame data length and extent bounds. Partial reads and WAIT
retain progress. The strict structure rejects unsupported variants instead of
copying the SDK's permissive bytewise string scan. Completion covers the selected
data chunk; trailing chunks/source bytes are ignored, not validated. No loop
metadata, floating PCM, extensible WAVE, RIFX or standard Yamaha tag0x20
admission is implied. Packed4 ignores byte-rate/block-alignment hints but requires
whole four-frame groups (two bytes per channel). Channel bytes interleave and
nibbles decode low first. An original integer AICA predictor/quantizer converts
these groups to PCM16, with separate channel histories, intermediate magnitude
saturation and final signed16 clipping. This differs from generic Yamaha ADPCM;
physical AICA equivalence remains unqualified. Encoded metadata reports4 bits;
the player still receives16-bit PCM.

Metadata remains available after a payload error. An unbound serialized seek
within the admitted frame range clears buffered input and terminal state. PCM8/16
seek is constant-time. Packed4 seeks reset histories and return MORE for nonzero,
non-EOF positions; call `kosdec_adx_wave_seek_step` until DONE or bind the reader
and let read advance the seek. Each step discards at most32 frames without filters
or PCM publication. Zero/EOF seek completes immediately. Cancellation remains
sticky until seek/reinit. Provider lifetime
and asynchronous staging stay with the caller, as for other byte sources. This
reader feeds native pipe/service/player WAV bindings described below.

### WAV playback bindings

Use `pipe_init_wave`, `service_init_wave`, `player_init_wave` or `player_bind_wave`
for the admitted PCM WAV reader. The existing private producer fiber performs all
provider reads and PCM conversion. Consumer callbacks only drain the PCM ring.
Production pause, full output and cancellation prevent further source work; source
release/cancel joins the producer before the reader/provider can be reused.

A reader-level PCM filter uses the existing per-channel interleaved stride contract,
after exact conversion and before pipe publication. Configure it only while unbound.
Seek/reinit likewise requires unbinding/join. Metadata publication, output-rate
limits and same-format rebind rules remain unchanged. WAV has no embedded ADX loop
counter, repeat/enqueue or loop-release support through these bindings; unsupported
commands return UNSUPPORTED. Software source completion is not audible completion,
and the existing retained sound allocation/Audio retirement limits still apply.
Wave/pipe/service/player state grows; rebuild consumers with matching headers.

## AU PCM and mu-law reader

`adx_au.h` provides a separate bounded AU reader for mono/stereo signed8-bit PCM,
big-endian signed16-bit PCM and G.711 mu-law. Use `kosdec_adx_au_init`, repeated
`kosdec_adx_au_read` on a producer, and `kosdec_adx_au_get_info` after admission.
The output is interleaved signed16. Each call makes at most one512-byte provider
request and emits at most32 frames; WAIT needs rescheduling. Input is byte-safe.
Unbound seek is constant-time and clears terminal state; filters run only on
emitted PCM. The caller supplies an immutable fixed extent and retires providers
independently. Both .snd and the baseline legacy .sd-NUL magic are recognized;
unknown-size payloads use the remaining extent. Selected-payload completion does
not validate trailing bytes. Other AU encodings remain unsupported. AU pipe, service and native player
constructors use the same producer and retained-output contracts as WAV.
`kosdec_adx_player_init_au` and `kosdec_adx_player_bind_au` keep input and
conversion on the private producer fiber. Release/join before seeking, changing
filters or reinitializing the reader. Rebind requires matching rate/channels;
container/encoding may change. Source drain is not audible completion.

## AIFF PCM reader

`adx_aiff.h` provides bounded mono/stereo signed8/BE16 PCM reads, metadata,
unbound seek and filters. COMM and SSND may occur in either order; chunk padding,
SSND offset and declared frame bounds are checked. Unknown chunks are skipped,
including INST/MARK; their loops are not interpreted. Empty audio may omit SSND.
Rates must be positive normalized integral80-bit values within1..UINT32_MAX;
fractional or larger rates are unsupported. No host floating-point conversion is
used. Each read makes at most one512-byte input request and emits at most32
frames. The caller owns source/provider retirement. Completion covers selected
frames, not trailing chunks/source bytes or alignment padding. AIFF-C and other
sample widths are unsupported; pipe, service and native player constructors follow the same producer-only
contracts as WAV/AU. Use `kosdec_adx_player_init_aiff` or, after release/join,
`kosdec_adx_player_bind_aiff`. Same-format output is retained; mismatched rate or
channels fail before new PCM admission. Rebuild consumers for added source state.

## SPSD PCM16 reader and playback

`adx_spsd.h` admits signed 16-bit little-endian PCM from 64-byte SPSD versions
01010004 and 00010004, codec 0 and index 0. The length field is even bytes per
channel. Mono allows a trailing span. For sample-interleaved stereo, pass the
exact record extent with a payload of twice the length field. This check
excludes aggregate-length and padded stereo records. Other codecs and layouts
remain unsupported. Loop and configuration fields are ignored.

Read makes at most one512-byte provider call and emits at most32 frames. Partial
input/WAIT retains state; unbound seek is constant-time, and filters run only on
emitted PCM. Provider retirement remains caller-owned. Metadata survives payload
errors; seek/reinit clears terminal state. Use `kosdec_adx_player_init_spsd` or
`kosdec_adx_player_bind_spsd` after releasing/joining the previous source, or the
corresponding pipe/service constructors. Existing producer-only I/O, initial
production pause, format publication, retained-output and same-format rebind
rules apply. Sequence/embedded-loop controls remain unsupported. Source EOF is
not audible completion. Rebuild matching source and player headers/archive.

## Prepared playback start

Call `kosdec_adx_player_set_wait_start(player, true)` before starting or while
prefilling to prepare a source without admitting its PCM to output. The private
producer still runs and fills its bounded ring. Keep polling, and query
`kosdec_adx_player_is_ready_start` or the `ready_start` info field. Readiness means
normal prefill (or short/empty source completion) and validated output format;
it does not mean audible playback or provider/device retirement.

A held prepared source has no prefill timeout. Setting the gate false admits it
on the next poll. Fresh output allocation/start is deferred; already-retained
output continues polling silence until admission. The gate persists across
rebinds. Closing it after a source starts affects later sources only, and never
pauses currently playing sound. Readiness clears on source release/cancel, new
source start, failure and stop. Setter/query do no I/O and require the serialized
owner thread. Cancellation and checked stop still retire producer users through
the existing join path. Rebuild player/info consumers after the layout addition.

## Loading-controller queue

`adx_loader.h` queues up to16 already-open immutable providers and sector ranges
into a ring/queue joint. It copies labels up to39 bytes, returns stable64-bit IDs,
and exposes pending order, source status and published byte/sector progress.
Labels do not implicitly open files. Resolve filename/AFS/disc sources separately;
providers stay borrowed until that entry reaches END or checked stop succeeds,
with independent backend retirement still required.

Start enters EXEC or empty WAIT; enqueue revives WAIT. Each owner-thread step
makes at most one512-byte provider call and services one sector. The default byte
low-water threshold is80% of initial joint capacity; new sector admission waits
while queued/borrowed DATA meets that threshold. An active sector finishes under
normal joint backpressure. A zero threshold disables this admission limit.
Pause preserves partial requests and prevents source work. Empty ranges finish
without I/O. Partial final sectors publish exact source bytes, without zero gaps.
With repeat enabled, a completed read moves its source to the queue tail and
increments `completed_passes`. The original ID and provider loan persist.
Disable repeat to let the queued passes finish.

Select `kosdec_adx_loader_set_lsc_flow` before starting for 2.00J
source-scoped flow control. The requested byte limit is captured when a
source or repeat pass first enters service. Updates take effect at the next
source. Reads wait while queued DATA meets the captured limit, excluding
held DATA loans. Zero blocks a source even with an empty joint. A refill
admits up to 512 sectors over bounded provider steps. Native joint wrapping,
exact short tails and incremental byte publication still apply.

For a linked player, choose `config.use_lsc_reload` and leave its loader
stopped over a fresh empty joint. The player's start or checked source
rebind starts the loader with the cached reload setting. Use
`kosdec_adx_player_set_linked_reload_sectors` or
`kosdec_adx_player_set_linked_reload_time` to change the next linked start.
For example, 0.2 seconds at stereo 44.1 kHz gives four sectors. During
playback, the current loader keeps its limit when it advances to the next
record. The default is 85% of whole input sectors, rounded down.

Use `kosdec_adx_player_get_input` on the player control thread for
queued ADX bytes and whole sectors. Joints report their identity and
queued DATA, excluding held loans. A direct read-at stream or the active
sequence entry reports only unconsumed bytes in its 512-byte staging
buffer, with a null joint identity and zero whole sectors. Nominal
buffered seconds become available with the validated source format,
during preparation or playback. Safety uses the live player setting.
The active loader keeps the value selected for transport. The identity
conveys no producer access.

Completed metadata remains queryable until its slot is reused. Stop cancels the
pending queue only after returning any held FREE span, preserves published DATA,
and never closes a descriptor. Close invalidates the controller after successful
stop; the caller may then explicitly seal the joint when no more input will be
supplied. Failure retains ownership until checked stop; the optional error hook
runs once and must not reenter. No hidden worker/global registry or automatic EOF.

Range entries can split one logical ADX file at sector boundaries and feed the
strict joint reader. For successive same-format, non-looping ADX files, call
`kosdec_adx_joint_reader_enable_linked` and bind the loader on the same joint with
`kosdec_adx_joint_reader_set_loader`. Enqueue each exact file extent before start.
The job or strict reader validates each footer and resets history at the next
header. Feed service can prefetch while PCM is full. Choose seal-on-empty for a
finite list, or use `LOADER_ENQUEUE` to post resolved file or AFS source loans to
the live producer. Open and borrow them on their ordinary owner thread. A
`take_receipt` success returns the loader ID. Hold the source loans through
producer join and backend retirement. `LOADER_FINISH` closes registration,
disables repeat and seals after pending reads complete. `SEQUENCE_REPEAT`
controls loader rotation and `RELEASE_LINK` finishes the current record.
For a reversible linked-follow setting, post `SET_LINK_FOLLOW` with
`enabled` false or true before the next record decision. The receipt
publishes the new flag. A final release or terminal cutoff cannot be
undone. This setting neither alters loader rotation nor changes
an already selected record. Close the reader and loader after producer
join, then retire any provider DMA or descriptors separately.

For a padded transport with the 2.00J buffered-header cutoff, enable linked
scan before binding the joint reader. At each validated footer, it examines
the two currently available contiguous DATA spans for the next header on
each span's halfword boundaries. A missing or incomplete header extent ends
the linked stream. Leave scan disabled when future input should be able to
complete the next header after a stall.

### ADX compressed-input policy

The loader now has an opt-in ADX input policy alongside its existing byte-flow
control. `loader_set_reload_sectors` sets a threshold in2048-byte sectors;
`loader_set_reload_time` derives that threshold from seconds, source channels and
sample rate; `loader_set_reload_default` selects the baseline85%of whole input
capacity sectors. Existing initialization retains its generic80%byte-flow default.
The ADX policy needs capacity of1..32767whole sectors and rejects out-of-range
sector arguments. Time settings clamp to capacity; invalid formats/NaN/Inf/negative
times leave state unchanged.

Time conversion follows binary32 rate*seconds, truncation to samples, whole32-frame
ADX blocks,18bytes/channel, then whole sectors. Thus one second of44100Hz stereo
selects24sectors, not the25sector approximation in the old manual's example.
Sub-sector settings can produce zero. In ADX mode, zero blocks new sector requests;
`loader_set_flow(0)` explicitly restores the legacy unlimited byte-flow mode.
Changing the threshold never cancels an active sector request.

`loader_get_buffer_info` reports queued/borrowed DATA separately, whole queued
sectors, nominal queued ADX seconds and safety at or above the current threshold.
ADX refill admission compares queued DATA only; generic byte-flow still includes
DATA borrowed by the decoder. Whole ADX groups determine time, so a partial group
adds no time. Queued headers/footers are counted as bytes: this is an input reserve
estimate, not proof that those bytes decode successfully, not buffered PCM duration
and not an audible clock. Supply the admitted ADX source rate/channels, independent
of output pitch. `loader_get_input_joint` returns identity without transferring
ownership or relaxing the existing loader/reader serialization requirements.

These controls and snapshots run on the serialized producer owner, before binding
or within its feed callback. Calling them concurrently from the player controller
is unsupported. No global server, implicit timer, transport cancellation or Audio
retirement claim is introduced. Per-handle/global servicing and source-independent
player exposure remain separately tracked H7 work.
### Player diagnostic reporting

`player_get_error` returns the first operational failure observed since init or
`player_clear_error`, with native errno, decoder status and player state captured
at observation. A zero code means no diagnostic is latched. Query and clear are
independent of the safety records in `player_get_info`: clearing a diagnostic
preserves `first_error`, `stop_error`, failed/stopping state and all resource
ownership. A failed player still needs checked stop. Bad arguments, normal input
waiting, short output supplied as silence and requested cancellation are not
reported as new operational faults. An unsuccessful initial constructor still
reports through its return value and errno; it has no initialized handle to query.

A per-player typed hook can be installed with `player_set_error_callback`.
Call `player_dispatch_error` from the serialized owner after polling or cleanup;
it returns one after delivery and zero when no callback or pending record exists.
No application error hook runs inside sound callbacks or the producer fiber.
Delivery preserves errno, occurs once per latched record and leaves the record
queryable. Installing a hook after a fault permits pending delivery; replacing
it after delivery does not rearm the record. Unregistering preserves the pending
record, while clearing discards it. The hook may copy its temporary snapshot but
must not retain the pointer, reenter player calls, block/yield or change FPU modes.
A later failure observation can latch again, including a failed stop retry.

These are native per-handle diagnostics with explicit dispatch, not CRI's global
text callback or numeric error-code ABI. Automatic stop/replay, SDK detection
thresholds and qualified output recovery remain tracked separately. Rebuild all
consumers for the extended implementation-owned player state.
### Owned filename sources

`<kosdec/adx_named_file.h>` adds a private `O_RDONLY` filename source over the
existing synchronous VFS reader. Open does not validate codec content; the first
decode does. It optionally uses the same caller-supplied 512..32768-byte compressed
cache. No extra PCM predecode, global file table or asynchronous I/O is introduced.

Initialize a handle and stream pointer to NULL, then call `named_file_open` and
`named_file_borrow`. Pass the borrowed stream to the existing decoder, service or
player. The single loan prevents `named_file_close` while that source is owned.
After successful player release/cancel/stop has **joined the producer**, call
`named_file_return`, then `named_file_close`. Return records the caller's completed
join; it cannot discover unreported users. EOF, decoder cancellation and drained
PCM do not return the loan. A failed join leaves the loan and file alive for retry.
The source can be borrowed again after return with its decoder state preserved.

Control calls belong to the creating ordinary IRQ-enabled KOS thread, outside
shared fiber carriers, hooks and incompatible locks. The serialized source reader
may run on the player's producer. The descriptor stays private and is never
exported, duplicated or retried. Keep the source/mount/media stable; no filesystem
shutdown, descriptor-table drain or forced wait interruption is permitted during
its lifetime. Only synchronous backends that retire their state on close failure
are supported. See the public header for the complete restricted contract.

Close returns -1 on a precondition/busy rejection, retaining ownership and outputs.
An admitted close returns 0, clears the handle, and reports any backend cleanup
errno separately. A cleanup error cannot be retried: the descriptor slot was
consumed and may already be reused. Constructor failure preserves its primary
errno and separately reports cleanup failure, leaving the output handle NULL.
This is native file ownership, not CRI ABI compatibility or completion of F1:
global close/reset, directory resolution and full service/reload policy remain.

The fault suite covers allocation/open/extent failures, constructor unwind,
wrong owner/IRQ state, outstanding loans, EOF, reborrow, close callback reentry,
backend errors and immediate descriptor reuse. The existing file-player matrix
also runs through named sources, including PCM comparisons, loop/sequence/seek,
I/O failure, cancellation and a failed producer join. Host models establish
adapter sequencing; actual VFS wrapper tests and target links remain distinct
from emulator playback or hardware/device-retirement qualification.

The native middleware batch adds common source/index loans, explicit filename
and player collections, bounded recovery policy, adjacent compressed-record
playback, atomic sequence-count queries, exact partition/provider resolution and
finite live SFA framing. See [NATIVE_MIDDLEWARE.md](NATIVE_MIDDLEWARE.md) for
composition, lifetime rules, baseline differences and unresolved qualifications.
These are independent native implementations; no CRI/MWPLY ABI is supplied.
