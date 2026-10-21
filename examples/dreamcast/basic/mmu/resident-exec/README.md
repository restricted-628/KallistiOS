# Resident executable VM fixture

This original primitive fixture loads fixed-layout A/B/A bytes from direct
ISO9660 `/cd` into a six-page executable reservation at `0x101ff000`. Five text
pages force instruction translation/refill beyond the four-entry ITLB; the last
page contains initialized data and a fresh BSS counter. Members have no imports;
they receive one resident service pointer.

Each first entry call yields from that resident service while five translated
returns remain pending. A fixture-owned live-use count refuses replacement,
then the child resumes and returns to resident code. A separately retained entry
pointer is called under another explicit use. Context remains selected across
all uses. Kernel does not discover raw users or provide this counter.

Between generations, quiescent retirement and six temporary ordinary page
allocations perturb the allocation layout. The A-to-B transition must change all
six physical pages; the final A permits reuse of retired B backing. These allocations are test
pressure, not an executable API memory requirement. Exact original file bytes
are compared against build-generated resident arrays before publish, and the
source is closed successfully. The fixture has no ELF/package parser. The
arrays add 40968 resident bytes, staging adds 4096 and pressure adds up to 24576,
plus allocation overhead; do not treat this as the later consumer's peak budget.

Source the assigned checkout's `environ.sh`, then use new output directories:

```sh
python3 build.py --out /absolute/new/build
python3 make-disc.py /absolute/new/build/resident-exec.bin /absolute/new/disc \
    --members /absolute/new/build --scramble "$KOS_BASE/utils/scramble/scramble"
```

`build.py` emits ELF, raw members, symbols, disassembly, commands and hashes. It
checks five page-aligned functions, exact data/BSS sizes and no unresolved symbols
or relocations. Build/link each complete fiber provider separately; never combine
core and addon objects. The normal environment selects core.

The disc builder requires macOS hdiutil and a built scramble utility. Its original
REIOS jump stub supports the existing emulator validation route; it is not a
validated physical-console boot image. The preflight seeds only a separate raw
mapping, checks translation/remap, and retires it before creating the VM space.
No executable VM page is preloaded. Preserve emulator limitations and prepare
hardware boot/loading separately when hardware is available.

Full success is exactly:

```
OVL3EXEC RESULT failures=0 loads=3 calls=6 yields=3 blocked=3 direct=1 translation=1 remapped=1
```

`remapped=2` also passes when both replacements change physical layout.
An incomplete log is not a pass. This fixture does not validate the later package
consumer, digest/token policy, physical cache/IIX behavior or timing.
