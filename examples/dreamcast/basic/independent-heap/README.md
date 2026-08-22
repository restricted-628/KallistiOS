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
