# Fixed-slot resident overlays

Optional `libkosoverlay.a` loads the independent `DCo@` v1 packages produced by
[`utils/kos-overlay`](../../utils/kos-overlay). The bounded software milestone
passes host and emulator tests; physical Dreamcast validation remains open.
It is deliberately not included in default addon discovery. Build explicitly
with `make -C addons/libkosoverlay` in the selected KOS environment, then link
`-lkosoverlay`. Public declarations are in `<kos/overlay.h>`.

## Supported scope

The established profile uses one caller-reserved P1 RAM slot, 32-byte aligned and
at most 1 MiB. Prove reservation in the linker map before the heap boundary,
below the kernel-stack reserve, and outside all resident code, data, stacks and
other slots. Address validation does not reserve memory. There is no
relocation, dynamic linking, TLS, constructor, unwind registration
or cross-overlay import mechanism. The host packager's restricted static-C
profile and original build recipe remain required.

For the optional translated consumer, create explicitly with
`ovl_slot_create_vm(space, &options, &slot)`. It owns a `vm_exec` reservation at
the fixed execution address, rather than treating that address as caller RAM.
Profile 2 requires 4 KiB base/size alignment and at most 1 MiB wholly inside the
VM arena. The space, context and ASID outlive the slot. Profile/backend mismatch
refuses during preparation, before unloading the old image. P1 calls retain
their original meaning; no automatic selection, relocation or demand paging.

Keep the application's coherent selected context continuously stable across
all live translated uses on all carriers, including suspended returns and queued
or escaped callbacks. Acquire uses Kernel's context check before returning a
view; it is not an enforced lease and adds no resume/inner-loop check. Resident
observers must not change selection. All controls and arguments remain directly
mapped and physically disjoint from managed backing; numerical P0/P1 addresses
cannot prove that physical exclusion.

The source is immutable regular `/cd` content on the direct ISO9660 backend.
Keep the mount, media, backend and VFS lifetime stable until return. Do not
perform global descriptor teardown, foreign error wakes or forced carrier/fiber
destruction. These restrictions ensure the private descriptor reaches detach;
a backend close error then consumes it and is reported without retry.
Reads are synchronous and block the carrier; there is no cancellation or total
deadline, implicit executor, ROMFS dependency, or competing DMA scheduler.

## Use and residency

1. Create with `ovl_slot_create()` for P1, or explicitly `ovl_slot_create_vm()`
   for translated ownership, then load using trusted resident expected
   package length and SHA-256. Expected identity is not read from the package.
2. Zero-initialize a stable resident `ovl_token_t`; acquire before exposing the
   view's entry/data pointers. Invoke the `int(void *)` entry from a resident
   wrapper with an explicitly compatible resident service context.
3. Retain the token across yields, preemption, resident helper calls and all
   return paths. Escaped data/callback users need their own residency through
   final callback return/deregistration. No overlay IRQ handlers.
4. Release from resident code after final return. Do not copy/move/edit live
   tokens or reuse storage while old borrowers/API callers remain.
5. Unload after all tokens retire. Destroy only when EMPTY and after externally
   joining all API callers/waiters; destroy is not a join primitive.

The optional resident observer is a normally returning notification only. It
cannot write/execute the closed slot, perform a nonlocal exit, or wait for its
own operation. Same-slot API calls during notification return `EBUSY`.
All APIs require thread context with interrupts enabled and permit short locks
and allocation; they are not usable under SQ/allocator/provider hooks.

## Replacement and failure

Admission closes while a load owns the operation. Live tokens or another
operation return `EBUSY` without changing the current image. Generation changes
only on successful publication; exhaustion returns `EOVERFLOW`, never wraps.

| Failure stage | Existing image |
| --- | --- |
| Allocation, open, size, header or descriptor preparation | Preserved |
| After unloaded notification, including first read failure | EMPTY |
| Digest or source close failure after retirement | EMPTY |

For VM slots, EMPTY execution admission does not mean backing is retired. A
failed abort/retirement retains its sole handles, closes admission and refuses
load/destroy. Retry `ovl_unload()` until it succeeds; retries do not duplicate
the unloaded notification. Never discard the slot on error. The original load
error and source-close error remain distinct; `ovl_unload()` reports its cleanup
error directly. Successful destroy then releases the empty VM reservation.

No failed load publishes an entry. The result reports primary error, source-close
error separately, and whether the new image was admitted. Old bytes can remain
in an EMPTY slot; their presence is not execution permission.

The loader retains/hashes the exact checked prefix, consumes the original ELF
once, hashes **all** bytes (including non-loadable metadata), scatters admitted
initialized spans and zeros exact BSS tails. Reordered/shared source extents
are allowed; overlapping destination spans are rejected. After digest verification
and close, it synchronizes the full slot's caches once, notifies loaded, and
reopens admission. I/O, allocation, free and observers run outside the mutex.

The VM path prepares a copied page plan before the unloaded notification, then
begins irreversible replacement. It scatters and zeros through synchronous
`vm_exec_write/zero`, never by dereferencing the execution VMA or passing backing
to VFS/DMA. Whole-package verification and private close precede executable
publication. It reuses Kernel cache/TLB retirement instead of P1's full-slot sync.
The page-plan scratch is bounded to 256 bytes for a 1 MiB slot; Kernel-owned
backing/table/metadata and failure-held resources add to the costs below.

Memory is one 32 KiB aligned staging allocation per active load, bounded local
prefix/descriptors/hash state and small slot metadata. Tokens are caller-owned;
there is no whole-image staging or allocation per nested call. This excludes
VFS/ISO9660/driver buffers, thread/fiber stacks and caller data. Full-slot cache
synchronization masks interrupts; the cap is not a measured latency guarantee.
SHA-256 binds content identity, not executable safety or authenticity.

## Validation

From `utils/kos-overlay`, run `python3 -B check_runtime.py` for actual addon C
under host OS/I/O shims with ASan/UBSan at `-O0` and `-O2`. Only the SH-4 SR read
is replaced in runtime source; state machine, parser, hash and cleanup compile
unchanged. Host `mmap` supplies a non-executable P1-address buffer; no SH-4 bytes
are executed. Coverage includes preparation preservation, destructive failures,
close errors, partial reads, identity, exact BSS, tokens, hook/thread interleaving
and 32 KiB crossings. SHA is checked against Python `hashlib`, including padding
boundaries and a million-byte message. Parser mutation testing is not exhaustive.

The original [`basic/overlay` example](../../examples/dreamcast/basic/overlay)
proves its resident linker reservation and loads two real fixed-linked members
through direct `/cd`. Its core and SH4ZAM-backed provider executables each pass
Flycast interpreter and dynarec runs: suspension retains residency, escaped
references block unload, replacement resets BSS, preparation failure preserves
the previous image, and digest failure after retirement leaves EMPTY.
These are separate complete provider links, not a mixed-provider executable.

Physical hardware/cache/timing tests remain open, as do hardware boot/shutdown
and measured stack/memory high-water marks. The REIOS fixture does not establish
those properties. No shared Kernel, Fibers or Storage interface is changed.

The VM consumer also passes actual-loader host fault injection against a modeled
VM boundary, including PREPARED abort followed by held retirement/retry. The
separate [`basic/overlay-vm` fixture](../../examples/dreamcast/basic/overlay-vm)
executes real profile-2 packages through actual VM ownership in both complete
providers, in Flycast interpreter/dynarec. It covers A/B/A backing changes,
page/staging crossings, independent-carrier preemption, suspended fiber returns,
retained yielding callbacks, context admission negatives and actual-VM cleanup
with link-time fault injection. Its README records the component memory costs
and distinguishes injected failures from physical failure modes. Neither suite
closes the hardware or whole-system high-water gates above.
