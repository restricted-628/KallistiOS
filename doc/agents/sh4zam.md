# SH4ZAM dependency and math contracts

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Graphics + fibers**.

## Scope and entry points

Pinned library integration, portable/SH-4 backends, direct math consumers and context-state compatibility.

- `addons/libsh4zam`
- `addons/include/kos/sh4zam.h`
- `kernel/arch/dreamcast/include/dc/sh4zam.h`
- `utils/check-sh4zam-source.py`
- `utils/Makefile.sh4zam-state`
- `examples/dreamcast/sh4zam/integration`

Relevant existing documentation (dated claims must be rechecked):

- `addons/libsh4zam/README.md`
- `doc/sh4zam-direct-graphics-math.md`
- `doc/sh4zam-affine-skeleton.md`
- `doc/sh4zam-consumer-audit.md`

## Preserve these boundaries

- Keep upstream source and attribution in the submodule. Do not edit vendor code, repin or copy inline implementations without a scoped request.
- Compare runtime and constant paths, compiler flags and backend selection. Approximation is not itself a bug; report domain, expected contract and reproducible inputs.
- Software XMTRX has shared/TLS state requirements across translation units; host tests do not prove SH-4 register behavior.
- For 3x3/3x4 helpers and fast memory APIs, check initialized lanes, alias/alignment constraints, state preservation and real consumers. Do not add blanket clobbers or paranoid checks to hide uncertain contracts.

## Coordinate before changing

- Graphics consumers
- Fiber alternative provider
- Integration/toolchain

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/sh4zam-release-test` — candidate `make -C utils/sh4zam-release-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/sh4zam/integration`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Normal-lane and packet experiments need bounded follow-up; no speed/accuracy certification from a source audit.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
