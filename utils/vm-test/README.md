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
behavior, or DMA/hardware validation. The target companion is
examples/dreamcast/basic/mmu/resident-vm. No production fault recovery is added.
