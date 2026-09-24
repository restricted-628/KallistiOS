# Caller-backed independent heaps

KOS independent heaps provide isolated allocation budgets inside memory owned
by an application or subsystem. They do not replace the process allocator and
have no startup cost, global registry, worker thread, or periodic work.

## Ownership and lifecycle

`mm_heap_create()` aligns the usable bounds of a supplied region inward and
stores all heap and block metadata inside that region. The caller retains
ownership of the region but must not modify or release it while the heap is
live.

Allocation, resizing, release, validation, and statistics are serialized by an
in-place mutex. They may block and are unavailable from interrupt context.
`mm_heap_destroy()` is an externally exclusive lifecycle operation: the owner
must prevent new calls and wait for every existing user before destroying the
heap. Destruction refuses a heap with live allocations.

## Allocation behavior

Every returned pointer is 32-byte aligned. Request sizes and backing-region
address arithmetic are checked for overflow. Free physical neighbors coalesce,
including reallocation shrink remainders, so a completely released heap returns
to one free block.

Invalid, interior, foreign, and already-freed pointers fail with `EINVAL`.
Allocation exhaustion and unrepresentable allocation sizes fail with `ENOMEM`.
A caller can use `mm_heap_get_stats()` to distinguish total free space from the
largest contiguous allocation currently possible.

## Validation and failure policy

Every mutating operation validates physical block placement, bidirectional
links, flags, sizes, totals, and live-allocation accounting before changing the
heap. A successor's entire header and minimum payload must fit within the
arena before any successor fields are read. Detected structural damage fails
with `EFAULT` before allocator mutation.

This is not memory protection or a security boundary. The heap handle must
point to readable, correctly aligned allocator metadata; its arena bounds and
mutex must remain intact. Arbitrary stale handles, damaged arena descriptors,
concurrent backing-memory writes, or inaccessible memory cannot be made safe
by inspecting block links. Callers must own live allocations and not access
them concurrently with free/realloc. Statistics output must not overlap heap
metadata or storage being concurrently modified.

`mm_heap_validate()` exposes the same whole-heap check for diagnostics and
destructive-operation preflight. It does not repair damaged metadata.

## Resource proportionality

The heap object, mutex, block headers, and payloads all occupy only the supplied
region. Code can be removed by section garbage collection when the API is not
referenced. A program that does not create an independent heap reserves no RAM
and performs no work for this facility.

This is a first-fit allocator with whole-heap validation: allocation, free,
resize, validation, statistics and destruction take O(number of blocks) time
in the worst case, while holding a mutex. Reallocation may also copy payload
bytes and calloc must zero its result. It is not a constant-time or IRQ-safe
allocator. Preallocate or use application-owned pools for latency-critical
render/audio loops. This extraction retains the existing validation policy;
it does not silently introduce a fast unchecked alternative.

## Validation

`utils/mm-heap-test` executes the production allocator with deterministic and
randomized allocation, resize, coalescing, alignment, statistics, overflow,
and invalid-pointer cases. `examples/dreamcast/basic/independent-heap`
demonstrates two separately budgeted heaps using the public API.

The separate white-box corruption test uses the production implementation and
checks twelve damaged-block layouts across seven public operations. It checks
that rejection does not modify block/payload storage; ASan/UBSan checks include
one-past-end and truncated successor headers. Host mutexes are pthread shims,
not proof of KOS scheduler behavior. Physical-hardware timing remains untested.

## Contribution boundary

`pr/independent-heaps` is based directly on current upstream, with no graphics,
fiber provider, SH4ZAM, cache/MMU policy, codec or private reference dependency.
It adds the allocator, public API, eight dynamic exports, tests and examples.
It does not replace malloc or change boot-time allocation policy.

### Validation snapshot (2026-09-24)

- Host production tests pass GCC 14 GNU17 and strict C23, Clang strict C2x,
  and Clang GNU17 ASan/UBSan. Randomized testing performs 20,000 operations;
  four pthread workers each perform 5,000 allocation/resize/free iterations.
- Corruption suite: 12 cases, 264 assertions, seven public operations.
  The pre-fix validator fails ASan on the terminal one-past-end successor.
- SH-4 GCC 16.2 narrow SDK build and focused allocator/example -Werror builds
  pass. Eight public definitions and generated export entries were inspected.
- The KOS-thread probe passes Flycast interpreter and dynarec on the narrow
  branch, integrated master and both complete fiber bundles:
  `HEAP-SAFETY: PASS threads=4 iterations=512 isolation=2`.

No physical-hardware performance result is claimed. The narrow upstream gthr
header build required a local explicit-C compile workaround; no unrelated
toolchain rule change is included in this contribution.
