# Low-level sound-output audit

## Scope

This audit covers the KOS-owned sound-RAM allocator, SH-4-to-AICA command
transport, one-shot sample playback, PCM/ADPCM streaming, channel observation,
and SPU memory transfers. Codecs, sequence players, instrument banks, authored
effects, and other content middleware are intentionally separate libraries.

## Completed software work

### Sound-RAM ownership

The allocator now validates its reserved range before constructing a pool,
aligns allocations for DMA and store-queue access, rejects use before
initialization and duplicate or unknown frees, and preserves an existing pool
if replacement metadata allocation fails. `snd_mem_available()` once counted
allocated extents; it now reports the largest free extent.

`snd_mem_get_status()` returns one coherent snapshot containing pool geometry,
allocated and free byte counts, largest free extent, and extent counts. It does
not allocate memory or create a background task.

### Command transport

Command submissions are serialized with a recursive mutex. The established
stop/submit/start batch remains valid, while another thread can no longer
insert a command into a stopped batch. Before touching shared sound RAM, KOS
validates the firmware queue's data range, size, alignment, head, and tail.
Submissions keep one word empty to distinguish full from empty and return
`EAGAIN` instead of overwriting unread commands.

The response reader now uses the queue's declared data base rather than the
queue header address, validates complete packet availability, and rejects
malformed sizes. The bundled firmware currently does not publish response
packets, so this is defensive closure of the public transport contract.

### Streams and one-shot effects

Stream initialization accepts one or two channels and explicit aligned buffer
geometry. Global state is published only after initialization succeeds.
Per-stream sound RAM and channel allocation is failure-atomic: a partial
allocation is rolled back and the handle never becomes visible.

Memory-backed WAV loading now has a bounded API,
`snd_sfx_load_wav_buf()`. It validates the RIFF limit, even-byte chunk padding,
format-before-data ordering, channel count, sample format, rate arithmetic,
frame geometry, data bounds, and the 65,534-sample hardware limit. The legacy
unbounded entry point remains for source compatibility but can only trust the
length stored in the supplied RIFF image.

Raw and WAV loaders pad internal transfer buffers rather than reading beyond a
caller's allocation. Stereo separation handles non-32-byte tails. Playback
rejects stale handles, invalid volume or pan, invalid loop ranges, unavailable
channels, and stereo playback beginning on channel 63.

Synchronized key-on now accepts a 64-channel mask. The firmware stages every
selected channel before issuing one global key-on execute, and stereo effects
and streams use this path. The advertised PCM stream-buffer maxima also fit
the AICA's 16-bit loop-end field after alignment.

### Exact SPU memory ranges

`spu_memload()`, `spu_memload_sq()`, `spu_memread()`, `spu_memset()`, and
`spu_memset_sq()` now honor the exact byte length. Aligned interiors continue
to use bulk, store-queue, or DMA transfers; unaligned heads and byte tails use
bounded G2 accesses. No primitive reads past a caller buffer or overwrites a
neighboring sound-RAM byte merely to round a request to a word.

### Asynchronous sound-RAM requests

The same exact-range behavior is now available through caller-owned queued
requests for both uploads and readbacks. Compatible endpoints use G2 DMA in
either direction; other transfers use bounded 4 KiB PIO chunks. Status reports
the requested and active transport, valid completed prefix, terminal errno,
and callback disposition. Requests support execution deadlines, independent
wait deadlines, cancellation, and terminal callbacks outside interrupt and
transfer-worker context.

The request worker uses a 16 KiB stack and the callback dispatcher retains the
conservative default stack for arbitrary application code. Both are created
only on the first asynchronous submission. No buffer, thread, periodic poll,
or request pool is reserved by `spu_init()`.

### Observation

`snd_channel_get_status()` samples firmware position and the hardware playback
bit while holding the G2 lock. Existing `snd_get_pos()` and
`snd_is_playing()` now reject channel numbers outside 0 through 63.

## Resource model

These facilities preserve KOS's link-time, pay-for-use model. They add no
startup thread, periodic poller, permanent stream buffer, or service fiber.
The optional asynchronous transfer workers are lazy and sleep when their
queues are empty.
Stream separation memory is still allocated only by `snd_stream_init_ex()`,
and per-effect or per-stream sound RAM is allocated only on request.

## Explicitly deferred

Automatic stream polling is also not part of the base driver. The existing
explicit poll API remains deterministic and allocation-free. A future opt-in
service may be considered separately, with measured stack use and explicit
lifecycle, rather than imposing a thread on applications that do not request
one.

Complete channel controls, checked stream status, DSP routing, and optional
content playback remain staged work. Their ownership and order are recorded in
`audio-capability-audit.md`.

## Validation

The modified allocator, command transport, stream manager, effect manager, and
SPU transfer code compile with the project SH-4 compiler. The shared command
layout also has a host-side test, and the embedded firmware is rebuilt from its
ARM source before publication. The ARM sources pass
`-Wall -Wextra -Werror`. SH-4 analysis also passes after suppressing GCC 16.2's
false double-free diagnostic on an existing NULL-initialized cleanup path;
manual inspection confirms that each allocation is released at most once. The
bounded memory-loading example builds against the new public API. Real
hardware remains necessary to validate AICA timing, DMA fallback behavior,
channel synchronization, maximum-buffer looping, and byte-tail accesses under
concurrent playback.
