# SH-4 cache-maintenance safety

## Scope

This change hardens the existing instruction- and data-cache APIs. It creates no
threads, allocates no memory, changes no cache mode, and does not enable the MMU.

## Address aliases

The SH-4 cache-control instructions do not operate usefully when their operand
names the direct P2 uncached area. Cache helpers therefore convert a P2 address
to the equivalent P1 cacheable alias before issuing line operations.

P0 and P3 addresses remain unchanged. They can name MMU translations, so
discarding their virtual address bits would operate on the wrong cache tag.

Alias normalization applies to:

- data-cache prefetch and allocation;
- line invalidation, write-back, and purge;
- data-cache range operations;
- instruction-cache range invalidation; and
- instruction/data-cache synchronization.

## Range validation

A cache range is described by a starting byte and a byte count. A zero-length
range is a no-op. A range is also rejected when `start + count - 1` cannot be
represented by `uintptr_t`.

After validation, both the first and final touched cache lines are aligned down
to the 32-byte line boundary. Iteration uses the inclusive final line instead
of an overflowing exclusive endpoint. Large valid data-cache ranges retain the
existing whole-cache thresholds.

These rules preserve the public void interfaces: invalid ranges return without
touching cache state rather than introducing a new error channel.

## Validation

`examples/dreamcast/basic/cache-safety` verifies P2 write-back and purge, the
write-back portion of instruction-cache synchronization through a P2 alias,
zero-length operations, and deliberately wrapping ranges. Success prints:

`KOSCACHE alias=1 overflow=1`

Compilation verifies the C and SH-4 assembly interfaces. Emulator and physical
hardware execution remain separate runtime gates because cache behavior cannot
be established by host-side tests.
