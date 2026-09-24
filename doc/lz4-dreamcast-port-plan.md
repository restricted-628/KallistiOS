# SH7091-informed LZ4 port plan

Study date: 2026-09-20. Baseline: `0b470baa` on `master`.
Status: design and compiler-footprint study, not an implemented optimization
or physical-hardware performance result.

Implementation follow-up: the first adapter-fix batch now primes frame scratch
at state/job creation (including the dictionary), preserves ENOMEM, and yields
after terminal jobs. `utils/lz4-adapter-test` adds allocation-failure and service
regressions. The second batch adds header requirements and opt-in per-decoder
block-size/scratch limits, with the Compact example selecting 64 KiB independent
blocks. These are not total resident-memory or queued-job limits. Shared worker
scratch, a caller-owned arena, direct-block decoding, and VMU save compression
remain unimplemented.
The study measurements below describe the stated baseline, not the new timing
of allocations.

First-batch validation: the new adapter suite passed GCC 14 and Clang GNU17,
GCC 14 strict C23, Clang strict C2x (using the existing upstream-warning
suppression), and Clang ASan/UBSan. The existing asset suite passed, the addon
and asset example rebuilt with SH-4 GCC 16.2, and the rebuilt example reported
`RESULT: PASS (complete compact asset)` in Flycast interpreter and dynarec
modes. These are software checks, not physical-hardware or throughput proof.

Second-batch validation: all four advertised block sizes in independent and
linked modes were checked against actual allocator requests, including tiny
payloads advertising 4 MiB blocks. One-byte-below limits fail before scratch
allocation; exact limits pass. Query failures preserve output, and bounded
constructors preserve ENOMEM behavior. The expanded suite passed Clang GNU17,
GCC 14 strict C23, Clang strict C2x, and Clang ASan/UBSan. Existing asset-loader
and model-converter tests passed. SH-4 GCC 16.2 rebuilt the addon/example, and
the example using Compact limits passed Flycast interpreter and dynarec.
No physical-hardware tests or reduced-scratch performance claims were added.

## Objective and scope

Make the optional LZ4 addon predictable in RAM use, stack use, and cooperative
scheduling, then optimize measured Dreamcast bottlenecks. Preserve the upstream
LZ4 1.10.0 implementation and its attribution. Keep KOS policy in the adapter,
build settings, and tests. No Sega SDK source or implementation is imported.

The first study covers the hardware sections relevant to compression and asset
loading: memory organization, alignment, caches, OCRAM/index modes, store queues,
cache allocation, and DMA ownership. It does not claim a cover-to-cover review
of every peripheral chapter. Graphics addon extraction remains separate.

## Reference basis

Local manuals were read without modification. Printed page numbers and PDF
page numbers differ; both are given where useful. The SH7091-specific manual
is the authority for this CPU. Generic SH7750 documents are cross-checks, not
permission to assume later SH7750R cache features exist on Dreamcast.

| ID | Source and selected sections | Relevance |
| --- | --- | --- |
| H | SH7091 Hardware Manual, `sh7091hm_2j.pdf`, sections 4.1-4.3, PDF pp. 65-73 / printed 4-1 through 4-9; section 4.6, PDF pp. 80-82 / printed 4-16 through 4-18 | Cache organization, software coherency, OCRAM mapping, prefetch, store queues |
| P | SH7091 Programming Manual, `sh7091pm_2j.pdf`, section 10.61, PDF p. 281 / printed 10-121 | `MOVCA.L` semantics and partially initialized cache lines |
| E | Hitachi SH-4 Programming Manual, `h14tp003d2.pdf`, section 2.5, PDF p. 35 / printed p. 21; data-address errors, PDF p. 116 | Natural alignment and address errors |
| S | Dreamcast/Dev.Box System Architecture, 1999-09-03, PDF/printed p. 13 | 200 MHz CPU, 16 MiB system RAM, shared external-memory traffic |
| A | `SH4_access990312_e.pdf`, pp. 1-2 | Access/transfer table explicitly describes simulation optimums, not achieved application throughput |
| L | Upstream LZ4 v1.10.0 block/frame specifications and the vendored library | Match-history rules, frame bounds, allocation and decoding behavior |

Local reference locations:

- H: `/Users/joseph/Documents/Sega Library Dreamcast SDK 2.00J/doc/sh4/sh7091hm/sh7091hm_2j.pdf`
- P: `/Users/joseph/Documents/Sega Library Dreamcast SDK 2.00J/doc/sh4/sh7091pm/sh7091pm_2j.pdf`
- E: `/Users/joseph/Downloads/dreamcast-docs-main/files/sh4/h14tp003d2.pdf`
- S: `/Users/joseph/Downloads/dreamcast-docs-main/files/official/DreamcastDevBoxSystemArchitecture.pdf`
- A: `/Users/joseph/Downloads/dreamcast-docs-main/files/official/SH4_access990312_e.pdf`

Public cross-checks:

- [Renesas SH7750-family hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware)
- [LZ4 1.10.0 block format](https://github.com/lz4/lz4/blob/v1.10.0/doc/lz4_Block_format.md)
- [LZ4 1.10.0 frame format](https://github.com/lz4/lz4/blob/v1.10.0/doc/lz4_Frame_format.md)

The key SH7091 cache, mapping, store-queue, and MOVCA pages were also rendered
and visually checked. Initial Poppler output omitted Japanese text; a second
render with PyMuPDF restored it. No conclusions depend on those incomplete
renders. Notes below paraphrase hardware behavior rather than reproduce SDK
algorithms or code.

## Hardware facts and design consequences

| Documented fact | Consequence for this port (engineering decision, not a speed claim) |
| --- | --- |
| 8 KiB direct-mapped instruction cache; 16 KiB direct-mapped operand cache; 32-byte lines [H 4.1] | Measure hot code size and data placement. Larger unrolled code or a large hash table is not automatically faster. |
| OCRAM consumes half the operand cache, leaving 8 KiB cache and 8 KiB RAM [H 4.3.6] | Do not enable OCRAM inside LZ4. It cannot hold the general 64 KiB match history, and competes with rendering scratch. |
| OCRAM's contiguous mapping changes with OCINDEX [H 4.3.6-4.3.7] | No private CCR changes or alias tricks. Any experiment needs KOS-level ownership, correct linker mapping, and restoration. |
| Word/longword memory accesses require 2-/4-byte alignment [E 2.5] | A 32-byte-aligned buffer does not make every token or match pointer aligned. Keep safe unaligned access and test every byte offset. |
| Cache/external-memory coherency is software-owned [H 4.3.8] | DMA transitions require the appropriate existing KOS ownership/cache protocol. Decompression alone must not flush whole caches. |
| Store queues are two 32-byte outbound queues; writes are longword/quadword and transfers are aligned 32-byte bursts [H 4.6] | Use ordinary CPU-readable memory for match history. SQ is a possible final upload stage, not a replacement output pointer for the decoder. |
| MOVCA avoids a line fetch on a write-back miss, leaving the other bytes unspecified [P 10.61] | Only consider it for exclusively owned complete destination lines that will be initialized before use. Never apply it blindly to partial lines or existing match history. |
| 16 MiB main RAM; CPU and transfers share the memory system [S p. 13] | Count compressed residency, final output, decoder scratch, stacks, dictionaries, queued jobs, and outstanding DMA together. Do not equate bus peak with decode bandwidth. |

LZ4 matches reference earlier decoded bytes and may overlap their destination.
Our default should therefore decode into the final, cacheable main-RAM buffer.
Retain upstream overlap handling; replacing match copies with general `memcpy`,
DMA, or SQ copies is not valid merely because the destination is aligned [L].
For texture uploads, finish and validate the RAM result before handing it to a
separately owned KOS upload operation. PVR command submission is not a general
byte-stream output sink.

The current linker places `.ocram` at `0x7c001000`, suitable for contiguous
8 KiB with OIX clear. H p. 72 gives a different contiguous example with OIX set,
crossing `0x7dfff000` through `0x7e000fff`. Existing graphics users must retain
control of that shared resource. OCRAM and OCINDEX are optional later trials,
not baseline requirements.

## Current implementation and measured baseline

The prior audit compared all four vendored C files, four public upstream
headers, and the license against release commit
`ebb370ca83af193212df4dcbadcc5d87bc0de2f0`: byte-identical.
The addon remains separately linked; raw assets do not require it.

Actual SH-4 GCC 16.2 preprocessing of this checkout selects:

- little endian, 32-bit `size_t`, current KOS `-m4-single` ABI;
- `LZ4_FORCE_MEMORY_ACCESS=1` (GCC packed access), not method 0;
- `LZ4_FAST_DEC_LOOP=0`;
- default `LZ4_MEMORY_USAGE=14`;
- `LZ4_HEAPMODE=0`; the addon sets only `LZ4F_HEAPMODE=1`.

Packed access is not the unsafe direct-dereference method 2. Benchmark method
0 versus method 1 with target disassembly and misalignment tests. Do not enable
method 2. LZ4_MEMORY_USAGE changes compression state and public static context
sizes; it is not a decompression-memory control. Never change it only in the
library while callers compile incompatible public headers.

Fresh isolated target compilations, same KOS environment, no production changes:

| Metric | `-O2` | `-Os` |
| --- | ---: | ---: |
| Whole `lz4.c` object `size` text column | 49,571 B | 42,873 B |
| Whole `lz4frame.c` object text, Frame heap mode | 10,797 B | 8,127 B |
| `LZ4_decompress_safe` symbol size | 868 B | 788 B |
| `LZ4_decompress_safe` own static stack frame | 68 B | 60 B |
| `LZ4F_decompress` own static stack frame | 92 B | 84 B |
| `LZ4_compress_fast` own static stack frame | 16,428 B | 16,428 B |

These are compiler/object measurements, not runtime high-water or throughput
results. Whole object text includes functions/constants that link-time garbage
collection may discard; it is not the linked application's hot working set.
Caller/callee frames, library helpers, callbacks, interrupts and fiber runtime
must be included in a real stack budget.

A separate `-O2 -DLZ4_HEAPMODE=1` compile reduces the compressor's own stack
frame to 32 B by moving state to heap, not by eliminating its RAM cost.
The current normal KOS thread default is 32 KiB (main stack 64 KiB), but fiber
stacks are caller-selected. Prefer reusable caller-owned `extState` compression
where on-device compression is needed; offline asset compression remains the
default. Do not present heap mode alone as a resource optimization.

Existing-adapter allocation probes from the immediately preceding audit:

- A first step publishing 7 bytes requested 65,540 B + 65,536 B from upstream
  for a 64 KiB independent frame: 131,076 B, excluding context and job state.
- A valid header advertising 4 MiB blocks requested 8,388,612 B on the first
  step even when actual output was tiny. This is over half of 16 MiB RAM before
  accounting for application memory. Reject or explicitly budget such profiles.
- Allocation failure was flattened to `EILSEQ`; distinguish `ENOMEM`.
- The service only yields on MORE, so a succession of terminal jobs can delay
  sibling services. Output budget alone does not bound time between yields.

For stock Frame decoding, buffer requests are `2*B+4` for independent blocks,
or `2*B+4+131072` for linked blocks, plus context/alignment/allocator overhead
(`lz4frame.c`, `dstage_init`). Multiple active states multiply this cost.
The current wrapper initially allocates little, so accounting only at job
creation misses the dominant later allocation.

## Port architecture to implement

### 1. General LZ4 compatibility layer

Keep upstream block/HC/Frame interfaces available, vendor sources unchanged,
and configuration explicit in the build. Their broad compatibility is useful;
the whole library should not reject valid large frames because Compact assets
choose a smaller profile. Preserve binary format interoperability and history.

### 2. Predictable Compact-asset adapter

Separate buffer ownership from queue length. Jobs should retain small metadata,
not each reserve maximum decode scratch while waiting. For the serial service,
design one reusable decoder workspace per active worker, reset between jobs.
Manually stepped concurrent decoders still need separate owned workspaces.

Expose a requirements/admission step covering supported frame profile, total
resident source, output, dictionary, scratch, and permitted outstanding jobs.
Then initialize fully before submission. A first implementation can retain
upstream Frame decoding with its custom allocator backed by bounded caller
storage, warm its lazy allocations before service execution, and reuse it.
The custom allocator interface is a version-sensitive static API: isolate it
inside the adapter, tie it to the vendor version, and test upgrade drift.
Do not hard-code an opaque context size from this compiler.

For the controlled converter profile, admit independent blocks of at most
64 KiB; keep legacy/general compatibility explicit rather than silently
changing the existing decoder's accepted inputs. No implicit large-frame
fallback, no mid-job growth, no hidden worker/stack, and no global cache mode
changes. Queue capacity is not a substitute for a total residency budget.

Scheduling must distinguish published output from actual work. Preserve the
existing byte-budget contract for callers that need it, but document that an
upstream call may decompress a whole block internally. A large output budget
can process multiple blocks: the existing unconditional one-block description
is too strong. Bound input/header work too, and yield after terminal jobs as
well as partial steps. Callback execution time remains a caller contract.

### 3. Lower-scratch, complete-block path (candidate, not yet implemented)

Evaluate a separate controlled-profile frame walker that calls upstream
`LZ4_decompress_safe` directly into the final resident output buffer. Independent
blocks with no dictionary need no extra history ring when output remains
resident. This can avoid the two generic Frame staging buffers, but requires
correct frame/header/block/content checksum handling and exact section bounds.
Use the public frame specification; do not invent an incompatible container.

Keep the existing upstream Frame path as an oracle and compatibility fallback
chosen explicitly by the caller. Differential-test any restricted walker
before adoption. This is more implementation and validation work than arena
ownership; it must earn its complexity with footprint and latency results.

A block-stepped API must advertise block-granularity publication, not secretly
violate the existing 7-/16-byte output budgets. At most one bounded block per
turn is a useful design target, not an exact microsecond guarantee. If 64 KiB
blocks are too costly, have the host tool flush smaller independent blocks
inside the existing Frame format and measure the ratio/latency trade-off.
Also cap work spent on empty blocks/metadata so output-only accounting cannot
be bypassed by a valid but adversarial stream.

### 4. Remove redundant work without weakening admission

The synchronous adapter computes section CRC incrementally, and the generic
asset loader then repeats it. Choose an explicit single integrity owner or a
private verified-result handoff tied to immutable bytes. Do not simply remove
the generic loader's check: arbitrary third-party decoder callbacks still need
validation. Retain enabled frame checksums and admission before publication.

Compare the current bitwise CRC with 16-entry (64 B) and 256-entry (1 KiB)
table variants. Any speed gain must be weighed against the small direct-mapped
cache. No repeated full-model validation during rendering is introduced.

### 5. Cache and I/O policy

Use cacheable main RAM for source/history/output by default. Align allocations
and DMA ownership spans to 32 bytes, while still supporting byte-aligned token
and match positions. Verify existing GD/G2/PVR transfer APIs' cache maintenance
before adding any; avoid duplicate flushes and partial-line data loss.
CPU-to-device handoff must publish dirty data first; device-to-CPU handoff must
not leave dirty aliases capable of overwriting incoming data. Keep ownership
exclusive across the whole transfer, not merely the decoded logical length.

Test source/output/dictionary placement at different offsets modulo the normal
16 KiB data-cache index period. Do not reserve large padding or cache banks by
default based on one synthetic test. No forced P2 decoding, cache-wide purge,
expansion-SRAM dictionary, or private CCR manipulation.

Bounded input prefetch, aligned literal-copy kernels, and carefully constrained
MOVCA full-line writes are later benchmark candidates. Prove readable input
bounds, complete-line ownership, overlap behavior, and cache-safe publication
first. Keep scalar/tail paths. Do not change FPU state or enlist SH4ZAM for an
integer token/copy workload without a demonstrated need.

## Implementation order and acceptance gates

1. **Accounting and regressions:** allocation counts/high-water, frame-profile
   limits, ENOMEM preservation, fair multi-job scheduling, cancellation and
   callback lifetime; retain original failing probes as regression tests.
2. **Predictable memory:** caller-owned reusable context/arena, complete prepare
   before submission, explicit compatibility policy, no allocator calls after
   admission through completion; measure all jobs plus executor stack.
3. **Work reduction:** complete-block direct-output candidate, one CRC owner,
   offline block sizing, unchanged wire formats and preserved checksums.
4. **Target tuning:** method 0/1 access, `-O2`/`-Os`, optional fast-loop variant,
   CRC alternatives and bounded prefetch. Compare linked size and entire
   loading workload, not only a tight decoder loop.
5. **Physical acceptance:** select defaults only after real-console throughput,
   worst-step latency, memory/stack high-water, and interference measurements.

Correctness corpus must include actual PCM2 models, incompressible bytes,
repetitions and short offsets 1-7, long matches, empty/tiny inputs, every source
and destination alignment modulo 32, all block tails, linked and independent
frames, dictionaries (including wrong IDs), truncation, damaged checksums,
overflowed lengths, trailing/concatenated/skippable frames, and limit rejection.
Exercise checksummed and non-checksummed input according to the declared
compatibility policy. Guard output and scratch boundaries and fuzz the parser.

Service tests must include many one-step jobs, callback requeue, queue full,
failure/cancellation storms, shutdown at each state, and a sibling service
whose progress is observed. Test allocation failure at every setup allocation
and forbid allocations after admission. Completion must not publish an asset
whose final integrity check failed.

Measure separately: storage read, decode, CRC/admission, and final upload.
Report compressed bytes, decoded bytes, ratio, total/peak owned RAM, requested
allocation counts, stack high-water, linked text/rodata, total time, maximum
non-yield interval, and time until another service runs. Compare against raw
assets as well as stock LZ4; no compression can be the better choice for small
or poorly compressible assets. Include active graphics/audio/I/O workloads.
Host timing cannot rank SH7091 kernels; emulator timing is not hardware proof.

## Reproducing this study's compiler checks

From the KOS root, source `environ.sh`, create a dedicated temporary directory,
and compile (replace OUTPUT with that directory):

```sh
kos-cc -dM -E addons/liblz4/lz4.c
kos-cc -O2 -fstack-usage -c addons/liblz4/lz4.c -o OUTPUT/lz4-O2.o
kos-cc -Os -fstack-usage -c addons/liblz4/lz4.c -o OUTPUT/lz4-Os.o
kos-cc -O2 -DLZ4F_HEAPMODE=1 -fstack-usage -c addons/liblz4/lz4frame.c -o OUTPUT/frame-O2.o
kos-cc -Os -DLZ4F_HEAPMODE=1 -fstack-usage -c addons/liblz4/lz4frame.c -o OUTPUT/frame-Os.o
kos-cc -O2 -DLZ4_HEAPMODE=1 -fstack-usage -c addons/liblz4/lz4.c -o OUTPUT/lz4-heap.o
```

Inspect `.su` files and use the same toolchain's `sh-elf-size`, `sh-elf-nm -S`
and `sh-elf-objdump -dr`. Study outputs are in
`/tmp/kos-lz4-sh7091-study.DQc2jX`; the earlier sanitizer/allocation probe is in
`/tmp/kos-lz4-audit.XI3uEP`. These temporary paths are evidence from this run,
not permanent build dependencies.

No runtime code, vendor source, public ABI, converter format, compiler default,
or cache mode was changed by this study. This plan adds work to the LZ4 detour;
it does not retroactively certify or invalidate unrelated graphics tests.
