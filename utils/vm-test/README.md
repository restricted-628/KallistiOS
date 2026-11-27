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

VM4 cases in `file-tests.inc` additionally compile verbatim production VFS
open/wrapper/reference/descriptor/seek/read/stat/close functions. Name-manager
lookup/retention, backend operations and platform mutexes remain substitutes;
the `resident-file` target separately exercises ISO9660 `/cd` with the explicit
direct backend. It does not test filesystem unpublish/drain. The suite
covers patterned loads/reloads, final zero padding, private staging, open-only
metadata checks, malformed counts, allocation/mapping failures, charge rollback,
raw mutation/ticket exclusion, overlapping pins, cancelled and parked I/O,
IRQ/BL/IMASK rejection, callbacks outside both locks, generic pre-detach close
failure versus post-detach backend failure, and failed-open unwind ownership.
The old VM1 zero-page purge assertion is retained; VM4 substitutes an exact
pattern-and-zero-tail assertion at the same prepublication boundary.

The opt-in host DP1 suite runs with `python3 utils/vm-test/run.py --demand`.
It compiles the actual private VM registry/recovery preparation and VM4/VFS
paths with `KOS_VM_DP1_PRIVATE` and `KOS_VM_DP1_HOST_TEST`. Default builds and
fault dispatch are unchanged. Host substitutes supply carrier/stack identities,
register reads, IRQ exclusion and generation wait/wake; neither the SH-4 return
bridge nor the final stop endpoint executes in this suite. Earlier target
continuation evidence is separate from these ownership tests.

Tests cover bounded registry/token budget, union residency reservations,
same-page sharing, different-page serialization, completion before enqueue,
spurious wakes, delayed seek/read and held candidate cleanup. Actual end,
close, evict, release and space-destroy calls refuse retained continuations;
successful retry/end releases references independently of VM3 physical pins.
Reload, final tail zeroing, consumed close errors and descriptor reuse exercise
ordinary quiescence. Thirteen isolated failure cases cover seek error/mismatch,
zero/short/prefix-error reads, allocation and page-table failures, cancellation
during I/O/before publication/before final retry, stable first error and invalid
selected context at final retry. Failed frames, source/staging and unrelated
owners remain retained; no terminal eligibility precedes candidate cleanup.

Failed-owner cases use child host processes ending with `_exit`, intentionally
leaving retained objects allocated. That is test isolation, not a target cleanup
or teardown protocol. The host profile reaches only the named test backend,
pthread wait/wake and allocation substitutes; no guest scheduler polling or
allocator debug callback executes. ASan/UBSan checks use:

```
python3 utils/vm-test/run.py --demand --cflags='-std=gnu17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
```

These tests do not establish a target P/D stack profile, actual direct-ISO
handler identity, hardware behavior or final stop execution. Production callback
bounds must include callees and remain valid throughout every admitted use;
checking current callback pointers once does not establish that lifetime.

The runnable target cold-read example is in
`examples/dreamcast/basic/mmu/demand-disc`. For the separate two-carrier
target test, compile `utils/vm-test/direct-disc-matrix.c` in its place and
link with the same complete opt-in archive plus
`-Wl,--wrap=fs_read,--wrap=genwait_wait`. Boot with the same generated ISO
asset. The gate and observation run in ordinary recovery context only. On
success it prints `DP1PAIR PASS shared=1 different=1 foreign=2 optical=2`.
Defining `DP1_FAULT_CONTROL=1` injects EIO on the parked read; value 2
cancels the file while two owners are retained. Those two intentional
fail-stop tests require an independent observer; their ARMED line is not a pass.
