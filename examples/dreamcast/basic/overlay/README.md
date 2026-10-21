# Two-member direct-disc overlay example

This original fixture loads and executes two independent `DCo@` packages in one
8 KiB P1 slot. It is not a CodeWarrior binary or an `MWo@` compatibility layer.
It uses actual direct ISO9660 `/cd` reads, not ROMFS or a host-memory substitute.

## Build

Select the intended KOS checkout and complete fiber-provider environment first:

```sh
source environ.sh
make -C examples/dreamcast/basic/overlay
```

Run from the KOS root. Python 3 and the selected SH-4 toolchain are required.
Output defaults to the example's `.build` directory; choose a fresh `OUT` for
another build. Existing output is rejected rather than silently replaced.
The optional overlay addon is built explicitly; default addon discovery remains
unchanged. Select either the integrated core fiber provider or the complete
SH4ZAM-backed provider at link time, never both and never a runtime toggle.

`overlays.json` assigns A and B to one 8 KiB P1 slot at `0x8c100000`.
`build.py` uses `utils/kos-overlay/project.py` to generate member linker bases,
the resident NOBITS reservation and trusted package catalog. It inserts the
reservation before `end` in a private copy of the default resident linker script,
then links the resident once. The shared linker script is not modified.

The final-link checker rejects missing/overlapping reservations and allocated
sections beyond the heap boundary or outside canonical P1 RAM. It checks the
explicit 16 MiB RAM budget with 64 KiB reserved for the kernel stack. The fixed
slot also consumes any gap between resident BSS and that slot; this is a simple
fixture layout, not a memory-optimal game layout. A larger resident must move the
slot and rebuild rather than overlap it. No MMU is required or enabled here.

Generated layouts, catalogs, size/headroom reports, maps, symbols and the final
resident check are retained in the output directory. `A.DCO` and `B.DCO` are
byte-identical aliases of `packages/member_a.dco` and `member_b.dco`, preserving
the disc fixture's names. Their expected identities come from the generated
catalog, not a second manual metadata path. Rebuild after project/member changes.

## Emulator disc fixture

On macOS, with KOS's `utils/scramble/scramble` already built, run from this folder:

```sh
python3 -B make-disc.py .build/overlay.bin .disc \
    --scramble ../../../../utils/scramble/scramble
```

Use a fresh output directory. Open `.disc/disc.cue` in Flycast with REIOS and
16 MiB RAM. This helper supplies an original minimal REIOS jump stub, not a
hardware boot/license bootstrap. Physical-console testing requires a suitable
boot workflow and remains separate. No SDK or game assets are included.

The guest must print:

```text
OVL2 RESULT failures=0 loads=3 unloads=3 yields=3 blocked=9 calls=4 direct=1
```

It executes A, B, then A again, confirming BSS reset and generation changes.
Each member calls a resident service which suspends its fiber with the overlay
return address still live. Replacement and unload must return `EBUSY`. After
return, a separate escaped-reference token still prevents unload until release.
An invalid header preserves the previous image; a damaged whole-image digest
after retirement leaves the slot empty. Observer callbacks cannot reopen it.

On an unexpected fiber failure the fixture reports failure and stops dependent
steps; it does not force-destroy suspended fibers or release their live tokens.
Token storage is resident and stable even on those paths. This is a finite test,
not a recovery manager or cancellation example.

Emulator execution is not proof of physical cache behavior, DMA timing, media
removal, or hardware shutdown. The example also does not validate arbitrary
compiled C features, service-executor jobs, translated executable memory,
asynchronous loading, or cross-overlay calls. See the addon README for the
restricted ABI, source-lifetime and ownership contracts.
