# Resident VM production regression

Run `make test`, optionally selecting `CC` and `CFLAGS`. Assertions are required.
`run.py` compiles kernel/mm/vm.c and the actual checked MMU table-management
section, using the real public VM/MMU headers. Only VM's SH-4 SR read is replaced.
MMIO/TLB/cache effects, allocation, IRQ state and KOS mutexes are host substitutes;
pthread mutexes exercise real concurrent callers of the production VM code.

The suite covers OCRAM/overflow/alignment admission, state/query runs and adjacent
reservations, strict commitment, zeroing, partial-range protection, no-op cases,
all backing and page-table allocation failures, rollback across table boundaries,
unchanged neighbor mappings/contents, failed output preservation, masked callers,
lock errors, independent contexts/ASIDs and inactive-context retirement.
Free spies reject backing still mapped or lacking cache/TLB retirement; commit
spies require the zeroed direct alias to have been purged before mapping.

This is not SH-4 execution, a physical cache model, a proof of KOS mutex/scheduler
behavior, or DMA/hardware validation. Target companions are the resident-vm,
resident-fault and resident-pin examples under examples/dreamcast/basic/mmu.

VM3 cases in `pin-tests.inc` exercise the actual manager code, including controlled
contiguous/fragmented/reversed host backing, byte edges, count/capacity errors,
unchanged outputs, ticket precedence, counter/allocation failure, overlapping
leases, unrelated VM2 resolution during admission, and queued enumerator drain
before release by a different thread. Host allocation and interrupt substitutes
are explicit; delayed consumer stages are a lifetime model, not DMA execution.
The resident-pin target probe separately exercises real fiber suspension and
carrier ownership transfer. No freed handle is used as a supported stale-handle
test. Both suites require assertions and complete result markers.
