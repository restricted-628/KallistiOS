# AICA, SPU transfers, DSP and audio streams

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **AICA/Manatee audio**.

## Scope and entry points

ARM command firmware, SH-4 transport, sound RAM, playback/DSP, PCM streams and bounded ADX support.

- `kernel/arch/dreamcast/sound`
- `kernel/arch/dreamcast/hardware/spu.c`
- `kernel/arch/dreamcast/hardware/spu_request.c`
- `kernel/arch/dreamcast/include/dc/sound`
- `kernel/arch/dreamcast/include/dc/spu.h`
- `kernel/arch/dreamcast/sound/arm` — firmware source, startup and image build
- `environ_dreamcast.sh` — ARM compiler/assembler settings (integration co-review)
- `utils/kos-chain/Makefile.aica.cfg` — AICA toolchain configuration (integration co-review)

Relevant existing documentation (dated claims must be rechecked):

- `doc/audio-capability-audit.md`
- `doc/sound-output-audit.md`
- `doc/stereo-sq-upload.md`
- `doc/sound-input-audit.md`

## Preserve these boundaries

- Track ARM firmware, SH-4 host transport and AICA DSP programs separately;
  their instruction sets, toolchains and validation are not interchangeable.
  Current firmware build flags select arm7di; verify actual compiler/ABI/output
  before changing flags. Do not inherit SH-4 SH4ZAM or language settings blindly.
- Treat Manatee as a separately versioned proprietary reference, not another
  name for KOS firmware or permission to import its driver/banks. Compare useful
  behavior and contracts using private evidence; preserve original sources.
- Follow firmware source through ELF, stream.drv, generated embedding and the
  actual host load. Check the prebuilt fallback separately for protocol drift;
  a successful SH-4 build does not prove the ARM source was rebuilt.
- Keep command/firmware capability negotiation and SH-4/ARM layouts synchronized; rebuilding host code alone does not update embedded firmware.
- Separate channel/RAM/transfer ownership, command acceptance and actual playback progress. Trace stream ring geometry, callbacks, underrun and teardown.
- The optional sound poller may block; do not silently move it onto a shared fiber carrier or add mandatory background cost.
- Stereo SQ mapping, sample order and tails need their own checks. Codec profile bounds do not imply general format compatibility.

## Coordinate before changing

- IRQ/G2 and cache/SQ
- Maple SIP capture
- Services for optional polling
- Storage for compressed input
- Integration for ARM toolchain/build changes; kernel retains shared G2/IRQ/cache
  mechanisms and global shutdown ordering. Audio owns its producer/drain contract.

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/aica-command-test` — candidate `make -C utils/aica-command-test test` in an assigned checkout.
- `utils/snd-dsp-test` — candidate `make -C utils/snd-dsp-test test` in an assigned checkout.
- `utils/sound-stream-test` — candidate `make -C utils/sound-stream-test test` in an assigned checkout.
- `utils/stereo-sq-test` — candidate `make -C utils/stereo-sq-test test` in an assigned checkout.
- `utils/adx-test` — candidate `make -C utils/adx-test test` in an assigned checkout.
- `utils/adx-pipe-test` — candidate `make -C utils/adx-pipe-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/sound/spu-lifetime`
- `examples/dreamcast/sound/channel-sync`
- `examples/dreamcast/sound/stream-service`
- `examples/dreamcast/sound/dsp-control`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

No imported proprietary banks/firmware. Host decoding and command models are not listening tests or physical sound-RAM DMA proof.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
