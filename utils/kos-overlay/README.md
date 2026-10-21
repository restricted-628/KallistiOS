# Overlay load-plan tool

Build-time components of the fork's overlay work; **not a runtime loader**.
It describes one already-linked ELF32 little-endian SH executable member within
an explicitly budgeted execution slot. No third-party Python modules are needed.

```sh
python3 elf_plan.py member.elf --slot-base 0x8c100000 --slot-size 0x2000 > member.json
python3 -m unittest -v test_plan.py
python3 check_target.py --tool-prefix sh-elf-
```

Link each member for its intended execution address. A translated-address build
uses that virtual address instead; changing the plan cannot relocate the code.
Separate text/read-only and writable load segments at page boundaries. The
default planning granularity is 4096 bytes, configurable with `--page-size`;
this does not assert that the target supports an arbitrary page size.

The deterministic, path-free JSON records image SHA-256/length, execution slot,
entry offset, ELF flags, and each PT_LOAD's file range, memory range, exact zero
tail, page coverage and ELF permissions. Gaps between segments remain gaps.
Offsets named `memory_offset`, `zero_offset` and `page_offset` are slot-relative;
`file_offset` is relative to the original ELF file. Zero tails are never rounded
up to an inferred alignment. Retain the original ELF unchanged with its plan.

Execution addresses, file offsets and physical backing are deliberately separate.
ELF p_paddr/linker LMA is not used as a runtime physical address or DMA target.
The same description can inform a direct-mapped or future MMU-backed loader.
Page spans can overlap when adjacent segments have identical ELF permissions;
they are coverage metadata, not independent allocation requests. Differing
permissions within one page are rejected rather than silently merged.

The tool rejects malformed load ranges, slot overflow, overlapping segments,
unbacked/unaligned entry points and unsupported loading actions (including
dynamic linking, interpreters and TLS). It does not inspect instructions, resolve
imports/relocations, process section tables, register unwind tables or invoke
constructors. It is a **layout check**, not complete executable/ABI certification
or a security boundary. ELF flags are descriptive: they do not promise SH-4 NX,
isolation, DMA suitability, physical contiguity or a valid hardware address.

The JSON is versioned host-build metadata, not a frozen target ABI or new archive
format. A loader still needs image admission, resident memory, cache/TLB
publication, execution/callback retirement and failed-load behavior. These are
not implemented here. Existing KOS ELF libraries and arch_exec are unchanged.

`check_target.py` requires a GNU SH compiler/binutils prefix (optionally an
absolute prefix). It compiles the small original fixture, links it at P1 and P0
addresses, and checks the plan against linker symbols. These are compile/link
checks only; the fixture linker script is not a complete application script.

Layout semantics follow the [System V ELF program-header specification](https://refspecs.linuxfoundation.org/elf/gabi4+/ch5.pheader.html).

## DCo@ v1 packaging

`dco_pack.py` adds stricter build-time admission and a deterministic package for
the P1 runtime profile and an explicit translated-VM profile. It leaves the
layout-only planner unchanged. Runtime loading is implemented separately in
the optional `libkosoverlay` addon.

```sh
python3 dco_pack.py pack member.elf member.dco member.json \
    --slot-base 0x8c100000 --slot-size 0x2000
python3 dco_pack.py verify member.dco member.json
python3 dco_pack.py pack translated.elf translated.dco translated.json \
    --profile vm --slot-base 0x101ff000 --slot-size 0x2000
python3 -B -m unittest -v test_plan.py test_package.py
```

Outputs must be fresh paths; input and existing outputs are never overwritten.
The packer verifies the complete in-memory pair before writing. An I/O failure
can leave an incomplete pair: publish only after successful command completion
and verification. The JSON is host metadata for generating trusted resident
expected length/digest values, not a target JSON parser. Do not fetch the expected
digest from the untrusted package it is meant to check. This is corruption/
identity checking for trusted application code, not authentication or a sandbox.

### Restricted static-C profile

Use unstripped ELF32 little-endian GNU SH4 ET_EXEC, ELF flags exactly 9,
System V ABI/version 0, a fixed execution address, and readable non-WX load segments.
Profile 1 (`--profile p1`, the default) uses a P1 address.
Slots are 32-byte aligned in base and size, nonempty and at most 1 MiB. The
host checks the possible P1 RAM window only; it does **not** prove installed RAM,
reservation or absence of aliases. The target must check its actual memory limit
and the application's linker reservation, heap, resident objects and stacks.

Profile 2 (`--profile vm`) instead requires base and size aligned to 4096 bytes,
a nonempty slot of at most 1 MiB wholly within `[0x1000, 0x7c000000)`, and one
permission intent per covered 4 KiB page. Execution addresses need not be
contiguous physical RAM; `p_paddr` still conveys no backing allocation policy.
The intended consumer uses the separate `vm_exec` owner/writer API and keeps
the caller-owned application context selected across all live uses. The fixed-P1
creation path explicitly refuses profile 2 before old-image retirement. The VM
consumer is implemented and its combined emulator/lifetime tests pass; see the
addon README for evidence and the remaining hardware validation limits.

Profile selection is explicit, never inferred from a package's address. Unknown
profiles and expected-manifest/profile mismatches are rejected. Profile 1's
serialized bytes and its 32-byte coverage rule remain unchanged.

Allowed allocated sections are `.text`, `.rodata`, `.data`, `.bss` and their
dot-suffixed subsections with matching type/flags and PT_LOAD byte mapping.
Metadata is restricted to `.symtab`, `.strtab`, `.shstrtab`, `.comment` and
nonallocated `.debug*` PROGBITS. Require one full symbol table and a defined
global function `_overlay_entry` at the ELF entry. Reject undefined/weak/common/
TLS symbols, arbitrary absolute imports (only `SLOT_BASE` is allowed), remaining
relocations, constructors/destructors, dynamic linking and unwind sections.
This intentionally rejects otherwise valid ELF outside the initial profile.

The source-level entry contract is `int overlay_entry(void *context)`, with
resident services supplied through the application-defined context. A symbol
table proves neither that C signature nor the safety of arbitrary pointers or
instructions. Use the original fixture/build recipe and matching application ABI;
do not present the check as arbitrary executable certification. The compile
fixture uses `-ml -m4-single -Wa,--isa=sh4`, freestanding/non-PIC code and disabled
unwind tables. Explicit assembler ISA selection prevents a default SH4A target
tag from silently entering this profile; no global KOS compiler flag is changed.

### Byte layout

All integers are little-endian, decoded explicitly. This is our own format,
not CodeWarrior header/debugger compatibility. `@` is part of the marker, not
the independent version field.

| Offset | Header field |
| --- | --- |
| 0 | bytes `44 43 6f 40` (`DCo@`) |
| 4 | u16 version 1, u16 header length 64 |
| 8 | u32 flags 0 |
| 12 | u32 segment count, 1–16 |
| 16 | u32 execution base |
| 20 | u32 slot bytes |
| 24 | u32 entry offset relative to slot |
| 28 | u32 ABI profile: 1 = fixed P1, 2 = translated resident VM |
| 32 | u32 original ELF offset, `64 + 32 * count` |
| 36 | u32 original ELF length |
| 40 | u32 total package length, at most INT32_MAX |
| 44 | u32 descriptor length 32 |
| 48 | sixteen zero bytes |

Each descriptor is six u32 values: memory offset, initialized file bytes,
memory bytes, original-ELF file offset, ELF permissions and alignment; then
eight zero bytes. The exact zero tail starts at memory offset + file bytes.
The descriptors are followed immediately by the **entire unchanged original
ELF**, with no padding or trailer. Non-loadable ELF metadata remains included
in package identity; a load-segment-only hash is insufficient.

The manifest distinguishes whole-package SHA-256 from original-ELF SHA-256.
Host verification checks the expected identity, re-admits the contained ELF and
reconstructs the package to reject descriptor/header disagreement. Determinism
means the same input bytes and slot produce the same output; the packer does
not normalize paths/debug metadata already present in the input ELF. Review
input debug/file-symbol information before distributing it.

The optional [runtime](../../addons/libkosoverlay) consumes/hash-checks
the whole package once via bounded staging, copies admitted file extents and
initializes exact BSS tails. Its `check_runtime.py` tests compile the actual C
with host OS/I/O shims under ASan/UBSan; they do not execute SH-4 code. Runtime
execution is checked separately by the P1 and explicit-VM examples; hardware
acceptance remains open. No direct-DMA or MMU execution claim follows from
package verification alone. `check_target.py` verifies real
GCC-built P1 and translated packages with explicit profiles, rejects translated
input under the default P1 profile and rejects existing output overwrite. These
are target compile/link and host packaging checks, not translated execution.

## Explicit game projects

`project.py` coordinates multiple fixed-linked members without choosing when the
game loads them. It reuses the existing DCo@ packer; neither package bytes nor
runtime APIs change. No compiler driver, ABI negotiation, asynchronous loading,
compression or demand paging is added.

Create a project manifest next to the game's build inputs:

```json
{
  "version": 1,
  "slots": [
    {"name": "game", "profile": "p1", "base": "0x8c100000", "bytes": 32768}
  ],
  "members": [
    {"name": "menu", "slot": "game", "elf": "build/menu.elf"},
    {"name": "play", "slot": "game", "elf": "build/play.elf"}
  ]
}
```

Addresses are examples, not a reservation in an existing game. Names are unique
lowercase identifiers (up to 48 characters; `count` is reserved). Unknown fields,
duplicate JSON keys, overlapping distinct slots and unused slots are errors.
Members sharing the SAME named slot are intentional. ELF paths are relative to
the manifest directory with no absolute paths or `..`; this is a trusted build
tool, not a filesystem sandbox. Numeric fields accept integers or hex strings.

1. `python3 project.py layout overlays.json build/layout`

   Generates `member_menu.ld`, `member_play.ld`, and `slots.ld`. Include the
   member's fragment before its normal member section script: it defines
   `SLOT_BASE`, not relocations or a new entry ABI. Continue using the documented
   restricted compilation profile and link-time section layout.

   Include `slots.ld` **inside the resident linker's SECTIONS**, after ordinary
   resident sections and before defining its heap-start symbol (normally `_end`).
   It emits dedicated `.dco_<slot>` NOLOAD sections at the selected P1 addresses.
   Do not also reserve a C array for those same slots. Fixed placement can create
   a gap below the heap: choose addresses deliberately and review the final map.

   An explicit `vm` slot emits no physical reservation. Its member still links
   at the selected VMA; backing belongs to `ovl_slot_create_vm` at runtime.

2. Compile/link the members, then run:

   `python3 project.py package overlays.json build/packages`

   Outputs `member_<name>.dco` plus each existing verification manifest,
   `dco_project.h/.c`, and `project.json`. Compile the generated C into the
   resident game. The catalog provides named slot/member IDs, slot options,
   explicit profile numbers, package filenames and trusted length/digests.
   Use the corresponding explicit P1 or VM constructor; copy slot options if
   adding application observers. Prefix filenames with your chosen `/cd`
   directory when loading. Retain existing token/ownership rules.

   The report includes initialized bytes, BSS, occupied extent and tail headroom
   per member. Tail headroom is not total allocator capacity or whole-system
   memory use. Internal segment gaps remain part of the occupied extent.
   IDs are sorted by name and can change when the project changes: recompile
   consumers, do not persist the numbers in save files or external protocols.

3. Check the **final resident ELF after linking the generated catalog**:

   ```sh
   python3 project.py check-resident overlays.json build/game.elf \
       --ram-end 0x8d000000 --stack-bytes 65536
   ```

   Select the RAM/stack budgets for the target; they are not detected by this
   host command. It checks exact dedicated writable NOBITS P1 reservations,
   allocated-section overlap and heap/stack bounds. `--heap-symbol` overrides
   `_end`. The initial checker supports resident allocated sections entirely
   within canonical P1 RAM, not resident P0/P2 aliases, OCRAM or custom regions.
   It intentionally refuses layouts outside that proof rather than certifying
   them. A VM slot's backing/context/alias ownership is NOT checked here.

Layout and package commands require fresh output directories and never overwrite
old builds. All semantic validation precedes output creation; an I/O error can
still leave a partial directory. Publish only after successful packaging AND
final-link checking. Wire the manifest, member ELFs and generated catalog into
the game's build dependencies; do not mix outputs from different project builds.
Generation is byte-deterministic for the same inputs. Project reports/catalogs
do not add host paths; the unchanged original ELF may still contain debug paths.

The final-link report binds the resident ELF hash, not arbitrary raw pointers,
installed RAM, actual stack high-water or malicious section/symbol metadata.
Existing target admission and application lifetime requirements still apply.

Validation: `python3 -B -m unittest test_project.py test_package.py test_plan.py`;
`python3 -B check_project_target.py --tool-prefix sh-elf-` checks actual SH-4
links and generated catalog compilation, including invalid final-link bounds.
These are host and compile/link checks, not new emulator or hardware evidence.
