# GAPS ownership and DMA

## Contribution placement

This implementation travels with direct access on
`fork/direct-disc-convenience`. GAPS/BBA ownership is a prerequisite of safe
direct G1-to-SRAM transfers, not a separate graphics or fiber-service addon.
The branch is based on the integrated fork and is not a clean upstream PR.

The upstream extraction needs separate, dependency-ordered reviews:

1. Existing `pr/asic-event-ownership` and `pr/g1-bus-ownership` prerequisites.
2. G2 transfer state, cancellation and completion lifetime mechanisms, without
   graphics or direct-disc default policy.
3. The direct-access series: GAPS exclusive ownership, SRAM leases, BBA
   lifecycle integration and native loader-call protection, including the
   tests for those invariants.
4. In that same series, direct-disc G1-to-SRAM integration after the
   direct-driver/request lifetime prerequisites; retain G1 access under
   NETWORK ownership. Keep the bridge and disc buffer-lifetime contracts
   together even when review requires separate dependency-ordered commits.

The last three are planned extraction slices, not published `pr/*` branches.
The application-fiber VRAM example belongs to `addon/fiber-disc` instead.
Loader hot-switching and real-hardware validation remain separate work.

## Ownership contract

The bridge has one 32 KiB SRAM window and one active software owner. Ownership
is not a hardware restriction on G1 addressability. Both STAGING and NETWORK
owners can authorize G1 disc DMA or G2 DMA through their SRAM leases.

Three lifetimes are distinct:

| Resource | Lifetime | Purpose |
| --- | --- | --- |
| Owner token | Driver acquisition through successful shutdown | Excludes another driver or independent staging user |
| SRAM lease | Allocation through explicit free | Assigns a buffer within the owner's window |
| DMA claim | Engine start through retirement | Serializes G1/G2 transfers across the entire window and prevents freeing the active lease |

A NETWORK owner must also coordinate the NIC's autonomous RX/TX accesses.
The DMA claim is not a NIC fence. The current BBA driver reserves all SRAM for
its RX ring, wrap guard and TX buffers; applications must not borrow those
addresses for independent staging. Allowing owner-authorized G1 does not add
a disc-to-network packet API or make concurrent buffer reuse safe.

## Lifecycle and migration

Replace `gaps_init()` with `gaps_acquire(role, &owner)` and `gaps_shutdown()`
with `gaps_release(owner)`. Pass that owner as the new first argument to
`gaps_sram_alloc()` and `gaps_sram_reserve()`. Rebuild callers: this changes the
fork's API/ABI. Repeated acquisition is rejected even for the same role; this
is not a reference count. Tokens are capabilities, not thread identifiers.

Acquisition clears SRAM. STAGING disables NIC interrupts, RX/TX and PCI bus
mastering. NETWORK is the deliberate KOS driver takeover path. Allocation,
free and ownership transitions require thread context. Stop admitting work,
drain workers/transfers and quiesce the NIC before freeing leases and releasing
ownership. Release refuses outstanding leases. A failed shutdown retains its
remaining leases/token; handle the error and retry after the cause is resolved.
Do not treat a failed shutdown as permission to initialize a different driver.

The BBA shutdown path closes TX admission and refuses release while existing
TX callers (including semaphore waiters) remain. Transfer cancellation and
lease release errors are propagated rather than discarded. This is resource
lifetime protection, not a complete dynamic driver manager; lifecycle calls
must still be serialized by their caller.

## Resident dcload

An IP or unclassified resident dcload loader blocks STAGING before register
writes or SRAM clearing. A known serial loader does not. Detection is
conservative and does not prove that an IP loader is using this specific BBA
rather than a different network adapter.

KOS NETWORK acquisition remains allowed for the existing network takeover
workflow. While KOS owns the bridge or transitions it, native loader calls are
rejected with `EBUSY`, except the existing workspace/host-info setup calls.
Supported console/file services must use KOS's network transport. That socket
backend does not implement `DCLOAD_GDBPACKET`; do not infer full GDB support.

Releasing KOS ownership does not detach or reinitialize the resident loader.
Its protection returns, so an IP-loader-booted application cannot enter staging
just by stopping networking. There is no force-steal or automatic hot-swap API.
Use disc or known serial-loader boot for standalone staging until an explicit
loader-detach/recovery protocol is implemented.

## Validation boundaries

Host tests exercise owner/lease lifetimes, loader refusal before writes,
transition exclusion and global G1/G2 serialization, including G1 under
NETWORK ownership. Target builds check compilation/linkage. Neither proves
electrical bus behavior, NIC quiescence timing or concurrent network shutdown
on physical hardware. Those remain hardware-validation gates. Emulator success
alone must not be reported as physical G1-to-BBA DMA proof.
