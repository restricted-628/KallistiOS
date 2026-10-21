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

`build.py` performs two resident links. The first finds the real `overlay_slot`
object in resident `.bss`; both member ELFs are fixed-linked at that address.
The second binds trusted package lengths and SHA-256 identities into the
resident application. It rejects a moved slot, overlapping sized symbols, a
slot outside resident NOBITS, or allocated RAM extending beyond the heap start.
The normal linker `end` keeps the heap after this reservation. Maps, symbol
lists, package identities and `build.json` are retained beside the binary.
Changing the resident link requires rebuilding the packages and identities.

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
