# Overlay load-plan tool

First build-time component of the fork's overlay work; **not a runtime loader**.
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
