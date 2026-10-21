# Fixed-slot overlays (OVL2 candidate)

Optional `libkosoverlay.a` loads the independent `DCo@` v1 packages produced by
[`utils/kos-overlay`](../../utils/kos-overlay). The bounded software milestone
passes host and emulator tests; physical Dreamcast validation remains open.
It is deliberately not included in default addon discovery. Build explicitly
with `make -C addons/libkosoverlay` in the selected KOS environment, then link
`-lkosoverlay`. Public declarations are in `<kos/overlay.h>`.

## Supported scope

One caller-reserved, fixed-linked P1 RAM slot per manager, 32-byte aligned and
at most 1 MiB. Prove reservation in the linker map before the heap boundary,
below the kernel-stack reserve, and outside all resident code, data, stacks and
other slots. Address validation does not reserve memory. There is no translated
execution, relocation, dynamic linking, TLS, constructor, unwind registration
or cross-overlay import mechanism. The host packager's restricted static-C
profile and original build recipe remain required.

The source is immutable regular `/cd` content on the direct ISO9660 backend.
Keep the mount, media, backend and VFS lifetime stable until return. Do not
perform global descriptor teardown, foreign error wakes or forced carrier/fiber
destruction. These restrictions ensure the private descriptor reaches detach;
a backend close error then consumes it and is reported without retry.
Reads are synchronous and block the carrier; there is no cancellation or total
deadline, implicit executor, ROMFS dependency, or competing DMA scheduler.

## Use and residency

1. Create with `ovl_slot_create()`, then load using trusted resident expected
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

No failed load publishes an entry. The result reports primary error, source-close
error separately, and whether the new image was admitted. Old bytes can remain
in an EMPTY slot; their presence is not execution permission.

The loader retains/hashes the exact checked prefix, consumes the original ELF
once, hashes **all** bytes (including non-loadable metadata), scatters admitted
initialized spans and zeros exact BSS tails. Reordered/shared source extents
are allowed; overlapping destination spans are rejected. After digest verification
and close, it synchronizes the full slot's caches once, notifies loaded, and
reopens admission. I/O, allocation, free and observers run outside the mutex.

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
