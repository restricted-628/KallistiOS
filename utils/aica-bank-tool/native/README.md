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
