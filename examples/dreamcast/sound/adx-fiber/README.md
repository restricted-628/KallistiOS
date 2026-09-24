# Synthetic ADX fiber/audio integration probe

Build with the KOS environment loaded:

```sh
make -C examples/dreamcast/sound/adx-fiber
```

There is deliberately no automatic run target. This produces roughly two
seconds of synthetic low-volume mono audio. No proprietary media, SDK data,
filesystem reads, or decoder-reference libraries are needed. Input is generated
incrementally into a 2,048-byte compressed queue with a 127-byte pending buffer,
rather than retaining the entire encoded stream. Partial writes retain their
pending suffix and EOF is published only after the last bytes are accepted.

The decoder uses one shared-executor fiber; a second runnable fiber checks
cooperative progress. Caller-owned compressed and PCM rings apply backpressure.
The decoder parks on input starvation as well as output congestion. Main owns
the sound stream and may block in KOS audio APIs without blocking the decoder
executor. The callback copies into stable scratch, releases ring space, and
wakes the producer. It does not decode, allocate, wait, or perform DMA itself.
The synthetic feeder on main has bounded work and never blocks. A real file
reader must use a separate ordinary loader thread so neither audio polling nor
the shared executor waits on storage I/O. See [input ownership and shutdown](../../../../doc/adx-input.md).

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

## Emulator results (2026-09-22)

The table below predates the translated-workspace change; it is not evidence
that the new MMU/OIX path passed.

SH-4 GCC 16.2 build, Flycast with HLE boot and serial output, synthetic chunk-fed
input. Host audio was muted; no listening test was performed. The runner closed
the emulator after observing the target result (the application remains open
after the guest finishes).

| Emulated RAM | SH-4 mode | Result | Producer starvations | Other fiber steps | Played bytes | Final PCM end |
| --- | --- | --- | --- | --- | --- | --- |
| 16 MB | Interpreter | PASS | 0 | 35,797 | 177,050 | 176,402 |
| 16 MB | Dynamic recompiler | PASS | 0 | 189,320 | 176,630 | 176,402 |
| 32 MB | Interpreter | PASS | 0 | 35,797 | 177,050 | 176,402 |
| 32 MB | Dynamic recompiler | PASS | 0 | 189,321 | 176,630 | 176,402 |

These are integration observations, not timing benchmarks. PASS is printed
after stream stop/destroy and executor cleanup. This test has not run on a
physical console and does not force upper-RAM placement. Flycast's expanded-RAM
setting does not establish physical bank wiring or cache-alias correctness.
Repeat on stock and modded machines, verify audible output and stop behavior,
then add explicit lower/upper-bank placement tests.

## MMU-on workspace bring-up

Default builds now keep decoder/pipe state in a dedicated 4 KiB translated P0
page at `0x0e000000`, backed by an owned canonical RAM allocation. All callers
use that single translated view. Input, PCM, stacks, and DMA scratch remain
disjoint P1 storage. The allocation's initial P1 cache lines are purged before
handoff; teardown joins users, retires the mapping, then checks an uncached
completion marker before freeing the allocation. The decoder itself remains
allocation-free and does not manage MMU/cache state.

Setup footprint on SH-4 is 4,096 bytes of backing storage plus a 4,100-byte
root context and one 6,144-byte second-level table (excluding allocator
metadata). A production application should reuse its owned context/table
rather than allocate a new address space per decoder. Default MMU startup
itself allocates none of these objects.

Build explicit controls with `-B` (make does not track command-line changes):

```sh
make -B ADX_MAPPED=1 ADX_OIX=0 # translated state, ordinary cache
make -B ADX_MAPPED=1 ADX_OIX=1 # translated state, virtual A25 selects OIX half
make -B ADX_MAPPED=0 ADX_OIX=0 # direct-P1 control, MMU still on
```

The standalone probe requires startup MMU, no existing context, and OIX/ORA off.
It owns the context and optional OIX transition for the whole test, not per
fiber switch. It refuses a runtime that does not apply the mapping, rather
than substituting P1 and printing a false translated PASS. An MMU-on direct
control PASS is not a translated-workspace PASS. No physical-hardware cache
performance or 32 MiB compatibility claim follows from these controls.

This exercises the native ADX core, not an implemented Sofdec video decoder.
See [MMU policy](../../../../doc/mmu-mapping.md) and the
[cache lifecycle test](../../basic/mmu/oix-workspace/README.md).
