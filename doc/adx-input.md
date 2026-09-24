# Bounded ADX compressed input

`<dc/sound/adx_input.h>` adds a caller-owned byte queue in front of the
[ADX PCM bridge](adx-pipe.md). It does not open files or create threads. The
native pipeline is now loader -> compressed queue -> decoder service -> PCM
queue -> ordinary sound poll owner. None of the queue or decode calls blocks,
allocates memory, starts DMA, or rewrites addresses.

## Ownership and backpressure

One loader owns `snd_adx_input_write` and `snd_adx_input_close`; one decoder
service owns `snd_adx_input_step`. Initialize before sharing. Byte storage and
both queue objects must be disjoint and remain valid until all owners stop.
Capacity is an explicit power of two from 32 through 1,048,576 bytes, independent
of file size and installed RAM. The queue uses lock-free 32-bit acquire/release
counters, including counter rollover.

Each write accepts only what fits and copies at most queue capacity in two
spans. Keep any unaccepted suffix: a zero or partial write is not EOF. A real
blocking file reader belongs on its own ordinary thread, not the shared fiber
executor or the audio polling owner. Bound its read buffer too; do not allocate
the whole movie merely to feed this queue.

After publishing bytes or closing, wake the decoder with `fiber_service_wake`.
It steps once and yields on MORE, parks on NEED_INPUT or NEED_OUTPUT, and exits
on a terminal codec status. The service executor's latched wake contract covers
the publish-before-wait race. Wake a loader waiting for space after a step
consumes bytes; use a predicate/recheck or latched notification on that side too.
The PCM consumer independently wakes the decoder after releasing PCM space.

One step borrows one contiguous queue span directly; it releases only consumed
bytes. Work remains bounded to 256 header bytes or 32 PCM frames. A physical
ring wrap with a second span already buffered returns MORE, not NEED_INPUT, so
the decoder never parks waiting for a notification that already happened.

## End of input, failure, and shutdown

- Close EOF only after every successfully read byte, including the pending
  suffix, has been accepted. EOF is final only on the last physical queue span.
  Closure is acquired before the write cursor so it cannot hide final bytes.
- Close FAILED for an I/O error. A subsequent step requests/acknowledges PCM
  pipe cancellation, with `result.input == SND_ADX_INPUT_FAILED` preserving the
  source-failure distinction. Keep detailed I/O errors in loader-owned status;
  do not report the cancellation as successful EOF. Already published PCM is
  retained, and a preexisting terminal codec status is not overwritten.
- Closure is immutable; repeating the same close succeeds. No writes are
  accepted afterward. `get_end` reports source closure, not decoder completion.
- The decoder can finish at its declared sample count with a trailer still
  queued. Stop and join the loader on codec completion/error/cancel; otherwise
  it could remain parked on a full queue forever. Wake it if needed. A cancel
  request to the PCM pipe does not itself interrupt a loader's blocking I/O.
- Stop callbacks, stop/drain sound, and quiesce both queue owners before any
  reset or reclamation. See the PCM bridge's teardown contract. There is no
  automatic seek, restart, or discard operation on a live queue.

## Validation and scope

`make -C utils/adx-pipe-test test` covers every truncation point of a small
stereo fixture with fragmented input, a closed queue wrapping into a second
span, EOF versus source failure, and three-thread mono/stereo pipelines with
3-byte and 137-byte loader fragments. Byte and PCM counters are exercised
across uint32 rollover, with every decoded sample compared to the standalone
core. Source failure with a full PCM queue retains that PCM, and a late source
failure cannot rewrite codec DONE. Existing PCM backpressure and cancellation
tests remain enabled.

GCC 14 GNU17/C23, Apple Clang 16 C2x, Clang GNU17 ASan/UBSan, and a separate
ThreadSanitizer run passed on 2026-09-22. SH-4 GCC 16.2 rebuilt KOS and linked
the synthetic example. The input object is 524 bytes of text, zero static
data/BSS, and references no allocator or out-of-line atomic helper.

The example generates input incrementally into a 2,048-byte queue with a
127-byte pending buffer. It is not yet a filesystem player. Real storage
latency/error handling, audible output, physical-console behavior, and forced
upper-bank placement remain separate validation gates. No Sofdec demux/video,
seek, looping, SDK implementation, or commercial assets are included.
The [example results](../examples/dreamcast/sound/adx-fiber/README.md) record
successful 16/32 MB interpreter/dynamic-recompiler Flycast runs, with the
limitations of expanded-RAM emulation stated explicitly.
