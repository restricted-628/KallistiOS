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

### Exact SPU memory ranges

`spu_memload()`, `spu_memload_sq()`, `spu_memread()`, `spu_memset()`, and
`spu_memset_sq()` now honor the exact byte length. Aligned interiors continue
to use bulk, store-queue, or DMA transfers; unaligned heads and byte tails use
bounded G2 accesses. No primitive reads past a caller buffer or overwrites a
neighboring sound-RAM byte merely to round a request to a word.

### Observation

`snd_channel_get_status()` samples firmware position and the hardware playback
bit while holding the G2 lock. Existing `snd_get_pos()` and
`snd_is_playing()` now reject channel numbers outside 0 through 63.

## Resource model

These facilities preserve KOS's link-time, pay-for-use model. They add no
startup thread, periodic poller, permanent stream buffer, or service fiber.
Stream separation memory is still allocated only by `snd_stream_init_ex()`,
and per-effect or per-stream sound RAM is allocated only on request.

## Explicitly deferred

Synchronized key-on currently carries a 32-bit channel bitmap in the embedded
AICA firmware protocol. Channels 32 through 63 therefore cannot participate in
one synchronized start command. Correcting that requires a coordinated firmware
ABI change and regeneration of the embedded firmware image; changing only the
SH-4 source would make the checked-in source and shipped firmware disagree.

Automatic stream polling is also not part of the base driver. The existing
explicit poll API remains deterministic and allocation-free. A future opt-in
service may be considered separately, with measured stack use and explicit
lifecycle, rather than imposing a thread on applications that do not request
one.

## Validation

The modified allocator, command transport, stream manager, effect manager, and
SPU transfer code compile with the project SH-4 compiler. The C sources pass
`-Wall -Wextra -Werror -fanalyzer`. The bounded memory-loading example builds
against the new public API. Real hardware remains necessary to validate AICA
timing, DMA fallback behavior, channel synchronization, and byte-tail accesses
under concurrent playback.
