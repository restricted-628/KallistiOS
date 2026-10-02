# Nindows2 parity through a Kosh-based addon

Read root `AGENTS.md` and `notes.md` D15 first. Owning lane:
**Nindows2/Kosh addon**, including Conio integration.

## Accepted direction and open design

Build the Nindows2-parity effort around Kosh as an addon. The owner explicitly
anticipates substantial Kosh and Conio modifications; an unchanged wrapper is
not a requirement. Preserve licenses, attribution and source history. Feature
parity does not by itself require proprietary ABI/binary compatibility, a new
mandatory desktop, or changes to kernel startup defaults.

First define the parity matrix from versioned references and real use cases:
existing, missing, adaptation needed, deferred and unresolved. Distinguish
Nindows2 from older Nindows. Do not infer the entire original scope from a chat
summary, or promise every reference feature before the matrix is reviewed.

## Entry points and evidence

- `examples/dreamcast/conio/kosh`: current Kosh/Conio consumer and lifecycle.
- Other `examples/dreamcast/conio` consumers: compatibility and packaging leads.
- `addons/libkosh` and `addons/libconio`: editable in-tree source imports.
  Their READMEs pin upstream provenance and local build differences. Public
  headers route through `addons/include/kosh` and `addons/include/conio`.
  Existing Kosh depends on Conio; do not accidentally audit/link a different
  installed kos-ports copy. The owner chose direct sources, not submodules.
- Use supplied versioned Nindows2 documentation/headers/samples as behavioral
  evidence, not code or assets to transplant. Keep reference paths and research
  outside this fork. Verify binary mappings before any scoped disassembly.

## Reconciliation questions

Map command registration/execution, console input/output, UI event delivery,
debug/inspection facilities and lifecycle to the chosen parity requirements.
Inspect PVR ownership, drawing/font/resource paths, keyboard/controller input,
VFS access, allocation and error handling. Decide which parts belong in Kosh,
Conio or a separate addon layer based on evidence, not names alone.

Review blocking calls, event loops, threads and optional fiber integration; do
not assume fiber-safe blocking or require the service executor. Graphics math
must follow the fork's SH4ZAM policy where applicable. Coordinate PVR/video with
graphics, Maple/input with kernel, VFS with storage, scheduling with fibers and
port/toolchain packaging with integration before modifying shared interfaces.

## First deliverable and later validation

Produce a pinned source/dependency map, evidence-backed parity matrix, proposed
addon boundaries and one bounded implementation task. Do not rewrite Kosh or
Conio during orientation. Implementation requires an assigned isolated checkout.
Later checks should cover command/input behavior, ownership/init/shutdown,
existing consumers and target link/run behavior; avoid a large generic harness.
Keep source, host, SH-4, emulator and hardware evidence separate. Maintain the
assigned private handoff, not other lanes' records.
