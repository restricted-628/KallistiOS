# Native ABK draft validator and load plan

Private implementation of the accepted host draft in the parent directory.
No installed/public header, firmware wire format or default build change.

`abk_plan_build` validates an immutable complete file and writes caller-owned
sample/zone arrays plus a plan only on success. Sample metadata separates absolute
file offsets from relative PCM payload offsets. All data loads are byte-safe;
all extents/counts are bounded before arithmetic or pointer construction.
Outputs must be naturally aligned and disjoint over used spans, as specified in
`abk.h`. A failed parse, insufficient capacity or alias leaves every output byte
unchanged. Unused array capacity is untouched, even when capacity is UINT32_MAX.

`abk_plan_place` checks whether the payload fits in an **already owned** contiguous
retail sound-RAM reservation. The caller supplies the reservation's real size.
It excludes the firmware region below 0x30000 and caps the end at 2 MiB. It does
not inspect allocator state or prove ownership, transfer readiness or final fetch.
Metadata stays in main RAM. Only the padded PCM extent is planned for transfer.

`abk_plan_chunk` divides a successful placement into32-aligned extents of at most
32KiB. It checks the committed prefix, limits and source/destination arithmetic.
End-of-payload returns a zero-byte extent that must not be submitted. This is a
software plan only; actual completion must come from the existing SPU request.

Main RAM must remain immutable and alive until all its consumers retire. Do not
call validation from interrupt/refill/voice processing: CRC is linear in at most
2 MiB, and zone intersection validation is bounded by 1024*1023/2 comparisons.
Arrays use 36 bytes/sample and 84 bytes/zone (maximum 95,232 bytes in caller RAM),
plus a 20-byte plan. Parser working storage is constant; no heap, I/O or MMIO.

```sh
make test
make sanitize
```

The harness executes the actual C at four byte alignments and compares acceptance
and every decoded field with the accepted Python oracle. It covers the original
truncation/bit-flip/semantic/random corpus, maximum counts, multi-record aliases,
32-bit boundary/overflow values, insufficient capacity, invalid output alignment,
input/output aliasing, output preservation and reserved-span placement.

Target links and disassembly are separate from host tests; no ARM firmware,
emulator, hardware, bank playback or upload claim follows from this component.
# Private upload transaction

`abk_upload.h` adds a caller-owned SH-4 transaction using the existing sound-RAM
allocator and asynchronous SPU service. It is private, and does not publish a
bank to ARM or start voices. Initialize `abk_upload_t` with `{0}`, call
`abk_upload_begin`, then pump from one serialized ordinary thread. Inspect
`state`, `committed`, `first_error` and `last_error`. Keep the source bytes,
descriptor arrays and transaction alive and unchanged until close succeeds.

The sound runtime must already be initialized and remain live, without concurrent
reset/shutdown. This transaction exclusively owns its allocation; no channel or
other consumer may reference it. `READY` means all PCM bytes were copied and the
final request was successfully destroyed. It does not mean playable bank or
safe unloading after playback.

Each request uses AUTO with an exact extent of at most 32 KiB. An unaligned source
uses the native PIO path; the loader adds no DMA/cache policy. The nonzero
per-request lifetime includes time in the queue. Pump neither blocks nor spins.
`EBUSY` from request destruction retains the request and is retryable; no next
chunk or READY is permitted until destruction succeeds. Other service errors
latch `first_error`, stop admission permanently and retain uncertain resources.
`last_error` describes a later retry/close error without erasing the first cause.

Cancel is latched even if the active transfer wins its completion race. Pump until
it retires, then close. Close refuses a live request and never implicitly cancels.
Partial uploads remain unpublished and owned until close. The allocator's free
function is void: the loader clears/checks errno under the live/exclusive/no-reset
preconditions and retains the address on any reported error. This is not a new
checked allocator API or a hardware fetch-retirement guarantee.

`make upload-test upload-sanitize` compiles the actual parser and loader against
declarations extracted from the current native SPU header. Narrow fake services
inject allocation, submission, status, terminal, cancellation, destruction and
release failures. They also exercise real byte copies, chunk coverage, four source
alignments and final COMPLETE plus destroy-EBUSY with late cancellation. These are
host tests of the transaction, not execution of KOS workers, DMA or AICA hardware.
