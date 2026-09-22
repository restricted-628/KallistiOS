# ADX fiber-to-audio bridge

`<dc/sound/adx_pipe.h>` connects the bounded ADX core to a fixed caller-owned
PCM ring. It is a single-producer/single-consumer bridge, not a complete file
player or a new sound driver. No SDK or commercial media is required.

## Ownership and work bounds

- One producer owns `snd_adx_pipe_decode`: header work is capped at 256 bytes,
  and each audio step decodes at most 32 frames directly into ring storage.
- One consumer owns `snd_adx_pipe_read`, including zero-capacity status calls.
  It copies at most the caller's requested capacity, bounded by ring capacity,
  and releases slots only after the copy. A wrapping read uses at most two
  copies. There is no memmove, buffer growth, or per-block heap allocation.
- Read immutable format through `snd_adx_pipe_get_info`, not raw state fields.
  Initialization and reset require all users to be quiescent.
- Publication uses lock-free 32-bit acquire/release operations. Counter
  arithmetic supports rollover; capacity is a power of two. The decoder's
  normal 32-frame groups cannot straddle the ring end, and a partial final
  group terminates production. Compile-time checks reject targets without
  always-lock-free 32-bit unsigned-int atomics.
- The caller supplies two int16_t elements per capacity frame to admit either
  mono or stereo after header parsing. This is four bytes per capacity frame;
  mono uses only half the supplied storage. Choose capacity explicitly from
  your latency/memory budget, not the total movie size or installed RAM size.

The pipe has no thread, locks, callbacks, filesystem calls, DMA, cache-register
changes or implicit address conversion. It can use ordinary canonical KOS RAM
in either bank. This is not proof of upper-bank DMA or 32 MiB hardware behavior.

## Shared executor integration

On NEED_OUTPUT, the decoder service parks with `fiber_service_wait` rather
than spinning or blocking the carrier thread. After a consumer read releases
space, wake that service with `fiber_service_wake`. Its coalesced pending wake
must be preserved if it arrives just before the producer parks. Yield after
successful bounded decode steps so other ready services can run. If input is
not memory-resident, use the [bounded compressed-input queue](adx-input.md)
with an independent loader that wakes the decoder when new bytes arrive; do
not perform ordinary blocking file reads on the shared executor.

Use the existing ordinary sound polling thread (or main) for start, poll,
status and stop operations. These APIs still contain blocking synchronization;
they must not be called from the shared decoder executor. This bridge does
not change them into nonblocking driver APIs.

In the normal stream callback, copy available PCM into caller-owned scratch
and return that scratch. Keep it unchanged until the next callback or enclosing
start/poll return, following the stream API's source-lifetime contract. Do not
return ring storage and release its slots: the producer could overwrite it
before the sound driver copies it. The extra bounded scratch copy deliberately
keeps this ownership boundary safe. A future zero-copy adapter needs an
explicit source-retirement contract, not early slot reuse.

Short callback data is padded by the stream driver. `starved` identifies a
short read while the producer was still live; normal EOF silence is not producer
starvation. Producer errors are preserved separately from ring occupancy.

## Completion and cancellation

`drained` means the producer has published a terminal status and the software
ring is empty. It never means DMA completed or the last sample was audible.
Read the producer status: DONE, TRUNCATED, INVALID and CANCELLED are different
terminal outcomes. Already queued PCM remains available after errors or cancel.

An owner requesting cancellation must also wake a parked producer. The next
producer step acknowledges cancellation without further decoding. A step
already in progress may finish and publish its bounded group. No control call
may directly reset decoder state while that step could be running.

For natural playback completion, account for silence in the sound-buffer
timeline and wait until playback reaches the end of the last source samples.
The existing played-byte counter requires servicing more frequently than one
full sound-ring duration to avoid missing wraps. Then stop/drain hardware.

For abort/teardown:

1. Stop future polls/callback producers (remove automatic service registration
   and wait for its active callback if that service is used).
2. Request and wake cooperative decoder termination.
3. Successfully stop/drain the sound stream before destroying it. On timeout,
   retain the stream and any possibly live buffers; do not claim reclamation.
4. Join/destroy the executor only after no callback can still wake it. Reclaim
   ring, callback scratch, input, and stacks only after their owners quiesce.

## Tests and remaining gates

`make -C utils/adx-pipe-test test` checks admission, output backpressure,
cancellation acknowledgment, sticky terminal states, truncated input, and
concurrent mono/stereo producer/consumer operation with capacities 32/64/128.
Each concurrency case decodes 65,539 frames and compares every PCM sample to
the standalone core, including explicit uint32 cursor rollover. The standalone
core has its own independent-decoder differential tests.

[The synthetic ADX fiber example](../examples/dreamcast/sound/adx-fiber/README.md)
connects the pipe to real KOS sound APIs and a second runnable fiber. It is a
target integration probe, not verified audible playback. Required next gates:
physical target execution, audible continuity and scheduling measurements,
forced lower/upper-bank buffers, actual storage input, and resource-safe recovery
from actual audio/DMA errors. No Sofdec demux, video, seek, or looping is added
by this bridge.

Bring-up validation (2026-09-22): GCC 14 GNU17/C23 and Apple Clang 16 C2x
host lanes passed, as did Clang GNU17 AddressSanitizer/UndefinedBehaviorSanitizer
and a separate ThreadSanitizer run. Tests include cancellation requested from
another thread while the producer is backpressured on a full ring. The existing
ADX core suite still passes. SH-4 GCC 16.2 built the full KOS tree and linked the
synthetic playback probe; the pipe object has no allocator or out-of-line atomic
helper references. Subsequent emulator results are recorded in the example's
README; physical audio validation remains open.
