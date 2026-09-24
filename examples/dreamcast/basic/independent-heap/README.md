# Independent heap example

This example divides two caller-owned memory regions into isolated heaps. It
demonstrates the intended resource model: no heap exists and no memory is
reserved until the application supplies a region to `mm_heap_create()`.

The allocations are checked for the API's 32-byte alignment guarantee. The
example also prints the requested, reserved, free, and largest-contiguous-free
counts so fragmentation and per-subsystem budgets remain observable.

An independent heap is thread-safe but not interrupt-safe. Before destroying
it, prevent new operations, wait for existing users, and return every
allocation.

Build `heap-safety.elf` explicitly for the regression probe. It uses four KOS
threads to allocate, resize, verify and free 512 buffers in total, with yields
between operations. A second heap remains independent throughout the run.
It also checks zero filling, alignment, failed resize preservation, foreign
and double-free rejection, coalescing and busy destruction. Success prints:

`HEAP-SAFETY: PASS threads=4 iterations=512 isolation=2`

The probe disables disc initialization and uses no media or networking. Host
tests cover injected block corruption separately. Emulator execution checks
KOS integration, not physical-hardware latency or allocator performance.
