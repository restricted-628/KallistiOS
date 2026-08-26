# Audio capability audit

## Scope

This audit compares the Dreamcast audio facilities expected by complete game
runtimes with KOS's existing AICA driver, sound-RAM allocator, effect manager,
and stream manager. The goal is to improve those KOS facilities directly. It
does not introduce another runtime's symbols, object model, or required
service loop.

The base driver should own hardware transport, memory, channels, status,
streaming, and DSP routing. Instrument banks, authored sequences, codecs, and
spatial-audio policy belong in optional libraries built on that base. This
keeps ordinary applications from paying for content systems they do not use.

## Existing KOS strengths

KOS already provides a hardened sound-RAM allocator, serialized SH-4-to-AICA
command transport, WAV and raw-sample effects, PCM and ADPCM streams, exact
sound-RAM transfers, basic playback observation, and explicit polling. These
facilities are small, direct, and compatible with link-time section garbage
collection.

The current work also makes initialization and allocation failure-atomic,
bounds memory and packet access, and avoids permanent service threads or
buffers in the base audio path.

## Reference capability baseline

The complete reference material describes a 64-slot synthesizer. Each slot can
decode ADPCM and provides an envelope and time-variant filter. A sample is
limited to 65,534 frames. The DSP executes a 128-step program, stores its work
area in sound RAM, and exposes 16 effect-output channels with independent
level and pan.

The higher runtime divides those slots among sequence, one-shot, and streaming
ports. Its default split is policy rather than a hardware partition and can be
reconfigured. KOS should consequently expose all 64 channels through one
driver and let optional managers impose their own allocation policy.

The reference command system buffers up to 256 host operations and has the ARM
firmware inspect a submission request on a 4 ms cadence. Its host-side service
is expected to run once per video frame to move commands, update fade state,
and refresh cached status. This proves the need for batching, progress, and
periodic parameter evolution, but KOS does not need to reproduce a required
frame-loop call. Firmware-owned work and an optional lazy service are a better
fit for KOS threads and fibers.

The reference memory-transfer layer supports 64 handles and 64 queued
operations, CPU or DMA transfer, synchronous completion, immediate return,
status polling, and callbacks. KOS currently has strong exact-range transfer
primitives but not a common queued asynchronous sound-RAM request layer.

The reference stream model supports up to four AICA slots per logical port,
live volume, pan, pitch, direct level, effect input channel/level, filter level
and resonance, current played and transferred positions, and total frame
count. Its documentation explicitly treats underrun prevention and ring-buffer
geometry as application-visible concerns.

The content layer covers program, drum, sequence, one-shot, DSP-program, and
DSP-output banks. It supports priorities, release behavior, seamless sequence
chaining, live MIDI messages, tempo and beat position, fades, and on-memory or
incrementally supplied streams. These are capability targets for optional KOS
libraries after the base channel, status, transfer, and DSP contracts exist.

## Capability gaps

### Driver control and diagnostics

The firmware protocol needs a versioned capabilities response, execution and
malformed-command counters, coherent driver state, and an error vocabulary
that the SH-4 can query without guessing from silence. Queue utilization and
sound-RAM status should be observable through checked KOS structures.

The final reference host library and driver are not the same version, so KOS
must negotiate a versioned command capability structure rather than assuming
that a linked host library and embedded firmware always match.

### Complete channel control

KOS exposes basic start, stop, frequency, volume, pan, and position behavior,
but not a single checked channel API covering envelope, filter, loop, direct
and DSP routing, release behavior, and coherent live status. These controls
should extend the existing channel vocabulary rather than create a parallel
player.

### Stream lifecycle

Streams need checked start/stop results, coherent transferred and played
positions, underrun/error status, live pitch/volume/pan controls, and optional
fade/filter/routing controls. Explicit polling remains the allocation-free
default. Any automatic servicing must be opt-in, lazily initialized, and use
a measured stack or an application-provided fiber service.

### DSP routing

KOS does not yet provide a checked facility for loading an effects program,
configuring its work area, routing channels to it, or controlling effect
outputs. This belongs in the base AICA layer because it owns hardware state,
but effect-program content and authoring tools can remain optional.

Effect-program changes should be rejected or explicitly quiesced while routed
channels are active. The reference material warns that live replacement can
produce discontinuities or invalid DSP output; this should become an enforced
KOS transition rather than a prose-only warning.

### Content playback

Instrument-bank and sequence playback, fades, tempo control, and higher-level
port management are middleware concerns. They can be supplied as optional KOS
libraries after the channel, timer, status, and DSP foundations are complete.
Their implementation must not be required by applications using raw effects
or streams.

### Spatial audio

Listener/source transforms and attenuation are policy above the driver. A
future helper can consume ordinary KOS or SH4ZAM vectors and produce checked
channel pan, level, pitch, and effect-send values without placing scene state
inside the AICA driver.

## First closure tranche

The shared command ABI now carries an explicit 64-channel mask for synchronized
key-on. The ARM firmware stages every selected channel before issuing one
global key-on execute, so early channels cannot start while later channels are
still being armed. Stereo effects and streams use this group operation, and
channels 32 through 63 are no longer excluded.

The firmware accepts only complete, known-size command packets. Channel starts
validate sample type, frequency, level, pan, loop geometry, and sound-RAM
bounds before programming hardware. A malformed record whose size cannot
identify the next packet boundary is discarded as one queue snapshot instead
of trapping the firmware in a permanent retry loop.

The advertised PCM stream-buffer maxima now fit the AICA's 16-bit loop-end
field after alignment. This prevents the largest supported 8-bit or 16-bit
buffer from wrapping its loop endpoint to zero.

The original low-32-channel synchronized-start packet remains accepted by the
firmware for existing raw command clients. New KOS code uses the explicit
64-channel command.

The SH-4 can now issue a bounded status query and receive a versioned firmware
response containing negotiated features, uptime, queue occupancy, processed
and rejected command counts, malformed-packet count, and dropped-response
count. Sound initialization verifies the protocol and the synchronized-channel
feature before publishing the driver as usable. A mismatched or silent
firmware therefore fails explicitly instead of accepting commands it cannot
execute.

## Second closure tranche

Sound-RAM transfers now have a caller-owned asynchronous request vocabulary.
Uploads and readbacks share one serialized worker, expose coherent byte
progress, support queue and active cancellation, enforce optional whole-request
deadlines, and dispatch terminal callbacks from a separate thread-context
worker. The execution deadline includes time spent waiting in the queue.

Aligned requests can require bidirectional G2 DMA. Automatic requests use DMA
when every endpoint and the exact length are compatible, then fall back to
bounded PIO if DMA is unavailable or already owned. Unaligned or odd-length
requests remain exact: no source over-read, destination over-write, padding, or
rounding is part of the contract. Cancellation reports only the prefix observed
as transferred before the operation was stopped.

Both workers are created lazily on the first asynchronous submission. The
bounded transport worker uses a 16 KiB stack; the dispatcher retains KOS's
conservative default stack because application callbacks are not controlled by
the driver. The synchronous SPU primitives, high-level sound manager, and
ordinary hardware initialization create neither worker. Request objects and
their main-RAM endpoints remain caller-owned.

## Resource model

The first tranche adds no thread, fiber, periodic callback, permanent buffer,
or dynamic allocation. The second tranche allocates request objects and starts
its two bounded workers only after the first asynchronous transfer submission.
Applications using only synchronous sound retain the earlier resource profile.

Future optional services must be lazy. The default stream path remains driven
by the application, while service-thread or service-fiber operation is an
explicit lifecycle choice.

## Remaining order

1. Complete checked channel envelope, filter, routing, and coherent status.
2. Complete checked stream lifecycle, progress, underrun reporting, and live
   controls; then add an optional service adapter.
3. Add checked DSP program, routing, and output control.
4. Build optional bank and sequence libraries plus host-side content tools.
5. Add optional spatial helpers integrated with the established math stack.

## Validation boundary

The shared packet layout has a host-side golden test, and both SH-4 and ARM
sources must build in the same tree before the embedded firmware is updated.
Emulation can validate command flow and ordinary playback. Physical hardware
is still required to establish exact multi-channel start skew, maximum-buffer
loop behavior, DSP behavior, and recovery from malformed or interrupted
traffic.
