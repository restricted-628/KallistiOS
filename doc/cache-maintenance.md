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

Both endpoints must also fit in one 512 MiB address-area window. This is a
conservative API contract, including within P0: split larger requests at those
boundaries. For example, `0xbfffffe0 + 64 bytes` begins in direct P2 and ends
in translated P3. Normalizing only its first byte would target a different
range. The C helpers and assembly entry points reject it before cache work.
Checks occur once on entry, not as extra validation in the cache-line loop.

Data-cache ranges also normalize their endpoints once, before iteration.
Their loops use internal already-normalized line primitives; standalone line
APIs still accept P2 aliases. Existing volatile assembly and per-line memory
operands are unchanged. This avoids repeating the P2 mask/compare in each
iteration, which SH-4 GCC 16.2 otherwise retained at -O2.

After validation, both the first and final touched cache lines are aligned down
to the 32-byte line boundary. Iteration uses the inclusive final line instead
of an overflowing exclusive endpoint. Large valid data-cache ranges retain the
existing whole-cache thresholds.

These rules preserve the public void interfaces: invalid ranges return without
touching cache state rather than introducing a new error channel.

## Whole-cache purge workspace

The speed-optimized `dcache_purge_all()` implementation uses a 16 KiB aligned
static eviction buffer in each translation unit that emits the inline helper.
Size-optimized builds use an address-array walk instead. A caller that knows
the affected addresses should prefer line or bounded-range maintenance to
avoid pulling that workspace into its binary.

## Validation

`examples/dreamcast/basic/cache-safety` verifies P2 write-back and purge, the
write-back portion of instruction-cache synchronization through a P2 alias,
zero-length operations, and deliberately wrapping ranges. Success prints:

`KOSCACHE alias=1 overflow=1 area=1`

Compilation verifies the C and SH-4 assembly interfaces. Emulator and physical
hardware execution remain separate runtime gates because cache behavior cannot
be established by host-side tests.

`utils/cache-range-test` compiles the actual arithmetic helper shared by the
production header. It covers zero length, endpoints, line rounding, alias
selection, area crossing and wraparound. Its separate 32-bit register model
interprets the production instruction-cache assembly's entry path up to the
P2 jump, rejecting unsupported instructions. It does not simulate cache tags,
MMIO, instruction timing or full MMU translation. The pre-fix assembly fails
this model's new cross-area regression.

With the normal KOS environment, `make -C utils/cache-range-test codegen-test`
compiles runtime-argument range probes and checks SH-4 GCC's generated bounded
OCB loops for repeated alias masks. This is a compiler regression check, not a
physical-hardware timing or throughput measurement.

### Upstream contribution boundary

`pr/cache-range-safety` is based directly on upstream and changes only range
admission, alias handling, contracts and tests. It preserves upstream's current
volatile assembly and per-line memory operands; no blanket memory clobbers are
added. It adds no thread, heap allocation, cache mode or MMU-default policy.
The integrated fork's physical-tag scans and MMU page retirement are separate
work, not part of this branch. Store-queue ownership is also a separate topic.
ICINDEX/OCRAM/OCINDEX mode-specific maintenance is not validated by this slice.

Public architecture reference: the
[Renesas SH7750 family hardware manual, sections 3.3 and 4](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware)
describes direct P1/P2 aliases, translated regions and cache operations.

### Validation snapshot (2026-09-24)

The arithmetic suite passes 205,957 checks with GCC 14 GNU17/strict C23,
Clang GNU17/C2x and ASan/UBSan. The assembly entry-path model passes 40,480
cases. SH-4 GCC 16.2 builds and object-code inspection pass. Flycast interpreter
and dynarec execute the smoke probe on the narrow branch, integrated master,
and both complete fiber bundles; bundle audits still find one fiber provider.
Physical cache behavior, mode-specific indexing and general translated-MMU
execution are not established by these results.
