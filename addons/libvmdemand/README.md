# Opt-in read-only disc paging

Build KOS normally, then run `make -C addons/libvmdemand` in that same
configured checkout. Link the complete archive **before** normal KOS libraries:
`kos-cc … -Wl,--whole-archive addons/libvmdemand/.build/libvmdemand.a -Wl,--no-whole-archive`.
These four objects select the matching VM, MMU, ISO driver and return bridge
as one unit. Never mix objects from different KOS builds. Ordinary apps do not
link this archive; libkallisti, kernel startup flags and other fault handling
are unchanged.

See `examples/dreamcast/basic/mmu/demand-disc`. With MMU and direct CD-ROM
enabled, select one context/ASID, open a file with `vm_file_open`, then call
`vm_file_demand_begin(file, first, count, &use)` on the carrier that will
read those pages. Read the returned file mapping, not a prefetched copy.
On a data-read miss, the handler checks the admitted range and carrier and
saves the CPU context. It does no I/O or scheduling. Recovery on that carrier
loads the backing page via the retained direct ISO file, zero-fills past the
declared end, publishes a read-only mapping, and retries the original
instruction with its registers and errno restored. End each use before
eviction or close. A new admission reclaims idle read-only pages of the same
file as needed to stay within its resident limit. The requested and existing
live ranges, VM pins and active loads retain their pages. If too few pages can
be reclaimed, admission returns `EAGAIN` without eviction. A later admission
can fault a reclaimed page back in. Explicit prefetch still returns `EAGAIN`
at capacity. File reads can block indefinitely if the drive does not retire.

This profile requires ordinary directly mapped kernel-thread stacks, at
least 8192 bytes of spare stack at admission **and the faulting instruction**,
fixed immutable media and no global VFS shutdown or context switching.
Use the example's restricted startup and polled SCIF debug path. Keep all
recovery code, stacks, callbacks, buffers and synchronization resident. Do
not add scheduler polling callbacks, hold recovery dependencies, or select
another debug handler during a use. These are application-wide obligations,
not properties the VM can infer from a pointer. A backing read failure after
an accepted fault is deliberately fail-stop after in-flight I/O cleanup;
it is not a catchable error or an invitation to replace the disc. Use explicit
prefetch when the application must recover from media errors.

The build supports one exclusive fiber provider at link time; the public
paging interface does not admit fiber/service-child stacks. No executable,
writeback, per-process context switching or physical hardware claim.
