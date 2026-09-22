# Synthetic ADX fiber/audio integration probe

Build with the KOS environment loaded:

```sh
make -C examples/dreamcast/sound/adx-fiber
```

There is deliberately no automatic run target. This produces roughly two
seconds of synthetic low-volume mono audio. No proprietary media, SDK data,
filesystem reads, or decoder-reference libraries are needed. The entire input
is generated in memory for this diagnostic, not as a player buffering policy.

The decoder uses one shared-executor fiber; a second runnable fiber checks
cooperative progress. A caller-owned PCM ring applies backpressure. Main owns
the sound stream and may block in KOS audio APIs without blocking the decoder
executor. The callback copies into stable scratch, releases ring space, and
wakes the producer. It does not decode, allocate, wait, or perform DMA itself.

Software-ring drain is not audible completion. The example records the end of
the last source sample on the sound-buffer timeline (including inserted
silence) and waits for the stream's played-byte estimate to reach it. Polling
must occur more often than one full sound-ring duration; this remains an
estimate, not a hardware timing proof. Afterward it stops/drains and destroys
the stream before destroying the executor. A teardown failure retains live
resources and waits for reset instead of freeing potentially active DMA data.

The probe prints PASS/FAIL, producer starvation count, unrelated-service
progress, and playback counters. PASS alone does not establish glitch-free
audio, real-time bounds, exact target PCM, or upper-bank DMA correctness.
Expected end-of-stream silence may increment the generic stream underrun
counter; producer starvation is separately counted only while decoding is live.

This test has been cross-compiled, not run on a physical console. It does not
force upper-RAM placement. Repeat on stock and modded machines, verify audible
output and stop behavior, then add explicit lower/upper-bank placement tests.
