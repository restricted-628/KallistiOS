# LZ4 Dreamcast addon

Read root `AGENTS.md` and relevant `notes.md` decisions first. Owning lane:
**LZ4 addon**. This guide routes work; it is not a blanket redesign assignment.

## Scope and entry points

- `addons/liblz4`: upstream codec, frame API, build and fork adapters.
- `addons/include/lz4*.h` and related vendor headers: preserve provenance/API.
- `addons/include/kos/pvr_chunk_asset_lz4*.h`: fork decode/service contracts.
- `addons/liblz4/README.md` and `doc/lz4-dreamcast-port-plan.md`: recheck claims
  against current source; a plan does not establish implementation.
- `utils/lz4-adapter-test`: inspect its Makefile and allocation fixtures before
  running focused tests in an assigned isolated checkout.

Distinguish this addon from unrelated LZ4 copies nested in other dependencies.
Verify the recorded upstream version, commit and license before modifications.

## Contracts to audit

- Generic codec/frame functionality versus PVR asset and optional executor
  adapters. Do not make basic decompression depend on the service executor.
- Exact frame consumption, trailing/truncated input, checksums, dictionary and
  content-size handling; malformed input must not escape bounded buffers.
- Allocation timing, maximum block/scratch sizes, linked-block history, job
  multiplicity and output lifetime. An output-byte budget is not a CPU-time bound.
- Step progress, cancellation, completion, callback retirement and shutdown;
  preserve caller ownership until all consumers and transfers retire.
- Preserve upstream licenses/history; optimize only against demonstrated use
  cases and measured evidence, without blanket cache changes or memory clobbers.

## Interface partners and validation

Graphics owns asset formats and rendering/uploads. Storage owns input I/O and
DMA retirement. Fibers/services owns optional scheduling/provider contracts.
Kernel owns shared allocation/cache/DMA mechanisms; integration owns build
defaults and dependency publication. Coordinate changes at these boundaries.

Use small malformed-frame, bounded-memory and stepped-versus-one-shot cases,
then affected SH-4 consumers. Record exact versions/options and distinguish host
tests, target build, emulator and hardware. A codec test does not validate VRAM
uploads or storage DMA. Update the assigned private handoff with evidence and
one next action; propose central policy changes to integration.
