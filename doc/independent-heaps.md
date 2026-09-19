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
heap. Structural damage fails closed with `EFAULT` rather than following a
corrupt pointer or performing a partial allocator mutation.

`mm_heap_validate()` exposes the same whole-heap check for diagnostics and
destructive-operation preflight. It does not repair damaged metadata.

## Resource proportionality

The heap object, mutex, block headers, and payloads all occupy only the supplied
region. Code can be removed by section garbage collection when the API is not
referenced. A program that does not create an independent heap reserves no RAM
and performs no work for this facility.

## Validation

`utils/mm-heap-test` executes the production allocator with deterministic and
randomized allocation, resize, coalescing, alignment, statistics, overflow,
and invalid-pointer cases. `examples/dreamcast/basic/independent-heap`
demonstrates two separately budgeted heaps using the public API.
