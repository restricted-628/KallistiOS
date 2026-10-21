# Explicit translated overlay fixture

This original test composes `libkosoverlay`, the resident executable VM owner,
direct ISO9660 `/cd` reads and one complete fiber provider. It does not replace
the [P1 example](../overlay), require VM for P1 overlays, or add demand paging.
Build the optional addon first, in an isolated configured checkout:

```sh
make -C addons/libkosoverlay
python3 -B examples/dreamcast/basic/overlay-vm/build.py --out /tmp/overlay-vm-build
python3 -B examples/dreamcast/basic/overlay/make-disc.py \
  /tmp/overlay-vm-build/overlay-vm.bin /tmp/overlay-vm-disc \
  --scramble utils/scramble/scramble
```

Both output directories must be new. The disc builder uses macOS `hdiutil` and
an original REIOS jump stub; it is not a physical-console boot image. The default
link uses core fibers. Test the complete SH4ZAM alternative separately, with its
matching headers/archive and all five core provider objects excluded. Never mix
the providers. Build metadata retains compiler/commands and artifact hashes.

## Checks

- Explicit profile-2 packages at `0x101ff000`, twelve 4 KiB pages. Five functions
  occupy separate instruction pages; four pages of initialized data and BSS
  spanning three pages are checked on every invocation. Packages exceed the
  loader's 32 KiB staging size. No overlay page is manually preloaded into a TLB.
- Direct-disc A/B/A execution, fresh BSS and physical-backing changes. Temporary
  allocations force an A-to-B backing change; final A permits physical reuse.
- Actual selected-table, hardware-ASID and MMU-control samples across carrier
  preemption, fiber suspension and resumed translated returns. A resident helper
  spins without an explicit yield until an independent thread progresses.
- A separately retained callback runs on an ordinary thread, blocks in a resident
  helper with translated returns live, returns, joins and deregisters. Replacement
  stays busy until the final resident token release, not merely thread completion.
- Wrong table with matching ASID, wrong hardware ASID and translation-disabled
  acquire refusals preserve outputs. These run only without live translated users;
  the correct context is restored before execution. This is not unsafe-resume testing.
- Malformed header preserves the admitted generation; bad digest closes admission.
  Linker `--wrap` hooks inject a pre-begin refusal and a failure after an actual
  VM write. Actual Kernel abort/retire operations then exercise retained cleanup,
  blocked load/destroy, retry and single unloaded notification. These synthetic
  refusals do not claim to reproduce a physical device/cache failure.

The separate temporary-page preflight matches the published VM primitive fixture:
it seeds only that page, then unmaps it before VM reservation. This enables an
unchanged emulator's general-translation mode without bypassing overlay refills.
Context log lines are serialized so thread preemption cannot corrupt evidence.
Assertions panic rather than release a live continuation or failed cleanup owner.

The successful final line has zero failures, 5 loads/unloads, 6 calls, 3 fiber
yields, 3 callbacks, 24 busy refusals, 3 preemption observations, 3 admission
negatives, 22 context samples, direct=1, at least one remap and 2 cleanup cases.
Core and exclusive SH4ZAM links pass Flycast interpreter and dynarec. This is
software execution evidence, not hardware cache/IIX, DMA, performance, media
failure, real boot or loader-shutdown certification.

## Memory accounting

GCC 16.2 SH-4 layout, excluding allocator overhead:

| Resource | Cost in this fixture |
| --- | --- |
| Executable backing | 49,152 bytes; failure-held pages remain charged |
| Loader staging | 32,768 bytes per active load |
| Overlay slot | 92 bytes |
| VM executable/reservation/page metadata | 264 bytes; 300 while holding load/plan |
| Two-table capacity | 12,288 bytes; existing live tables may coexist with another 12,288 bytes of preparation fallback shells |
| VM space and root context | 4,144 + 4,100 bytes, separate from region charges |
| Tokens/views | 16/56 bytes each, caller-owned |
| Loader function's compiled stack frame | 1,480 bytes, **not** full call-chain peak |
| Fixture child stack | 16,384 bytes, plus provider context/control allocations |
| Extra ordinary thread stack | 32,768 bytes; heartbeat and callback carriers are joined sequentially |

The loader's 256-byte maximum page-plan scratch is already part of its stack
frame. Replacement does not retain two complete physical images: preparation
retains the old backing plus metadata/table capacity; begin retires it before
new backing allocation. Failed cleanup can retain the current backing/load.
The fixture additionally holds up to 49,152 bytes of artificial allocation
pressure while replacing; that is test machinery, not loader policy.

Do not call the table a whole-system peak. ISO9660's shared inode/data cache
payload is 65,536 bytes plus cache metadata; its separately allocated 32 KiB
async bounce buffer must be budgeted if that path has been used, but is not
requested by this synchronous fixture. VFS descriptors, directory snapshots,
driver/request state, main/system stacks, provider runtime/math contexts,
allocator alignment/fragmentation and consumer data add costs. Physical peak
memory and timing measurements remain open.
