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
- `utils/aica-music-client` — explicit private live owner/client; retained physical
  pool and bank ownership, no automatic reset/shutdown or natural completion.
- `utils/aica-sequence-tool/native` — private catalog, publication protocol and
  upload owner; `make catalog-test catalog-sanitize wire-test wire-sanitize
  client-test client-sanitize owner-test owner-sanitize`. Mocked transfer results
  establish host ownership behavior, not device execution or sample retirement.
- The same native directory provides `live-test live-sanitize live-owner-test
  live-owner-sanitize` for sequence control, clock, binding and host transport.
- `utils/aica-bank-tool` — private original bank authoring; `native/` contains
  byte-safe validation/planning and a caller-owned unpublished PCM uploader.
  This is not an installed bank API or ARM bank playback implementation.
- `utils/aica-sequence-tool` — original ASQ authoring and native admission;
  `native/catalog.h` owns caller-backed sequence resources through validation,
  play, pause and chaining. CPU data leases do not retire physical voices.

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

- `utils/aica-firmware-check` — required integration validation for ARM firmware
  changes: `make -C kernel/arch/dreamcast/sound/arm check-layout`. Explicit Python 3
  static image/stack/helper/state check; ordinary builds do not run it automatically.

- `utils/aica-command-test` — candidate `make -C utils/aica-command-test test` in an assigned checkout.
- `utils/snd-dsp-test` — candidate `make -C utils/snd-dsp-test test` in an assigned checkout.
- `utils/sound-music-test` — resident engine/bank and explicit four-layer cases;
  `make test sanitize layer-test layer-sanitize`. Shared lookup budget, no-self-steal
  and partial cancellation/failure are host evidence, not physical retirement.
- `utils/sound-music-live-test` — actual firmware backend/guards/clock and explicit
  host owner; `make test sanitize host-test host-sanitize`. Fake MMIO/transport
  is distinct from target execution; retained leases are not physical retirement.
- `utils/sound-port-test` — actual software allocator/port admission tests (`make test`).
- `utils/aica-music-client` — explicit private live owner/client; retained physical
  pool and bank ownership, no automatic reset/shutdown or natural completion.
- `utils/aica-bank-tool` — host authoring (`make test`); `native/` provides
  `make test sanitize upload-test upload-sanitize`. Mocked host services are not
  hardware transfers; native links and exact emulator readback are separate gates.
- `utils/sound-stream-test` — candidate `make -C utils/sound-stream-test test` in an assigned checkout.
- `utils/stereo-sq-test` — candidate `make -C utils/stereo-sq-test test` in an assigned checkout.
- `utils/adx-test` — candidate `make -C utils/adx-test test` in an assigned checkout.
- `utils/adx-pipe-test` — candidate `make -C utils/adx-pipe-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/sound/spu-lifetime`
- `examples/dreamcast/sound/port-reservation` — software ownership only; no playback.
- `examples/dreamcast/sound/music-layers` — original four-layer fixture with
  coherent sample-address snapshots, sustain/release and retained ownership.
- `examples/dreamcast/sound/music-live` — original explicit MIDI fixture; retains
  played bank and pool after logical quiesce, then remains alive.
- `examples/dreamcast/sound/sequence-resources` — original ASQ publication, query,
  drop and cancelled-upload fixture; no sequence playback or physical samples.
- `examples/dreamcast/sound/sequence-live` — original playback/pause/continue/chain
  fixture; CPU program retirement is separate from retained tone/pool ownership.
- `examples/dreamcast/sound/channel-sync`
- `examples/dreamcast/sound/stream-service`
- `examples/dreamcast/sound/dsp-control`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Resident music and sequence services continue independently of command-queue
processing. Do not replace/clear DSP programs while those producers can run:
the current exclusive command-queue operation is not an acknowledged ARM
service-boundary pause. Acknowledged exclusion/resume and physical DSP-work
retirement remain separate unresolved contracts. Current live fixtures use no
DSP sends; their success does not qualify this composition.

No imported proprietary banks/firmware. Host decoding and command models are not listening tests or physical sound-RAM DMA proof.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
