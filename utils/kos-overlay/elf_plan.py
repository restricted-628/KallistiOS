#!/usr/bin/env python3
"""Describe a linked SH overlay without choosing its physical backing."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
PROGRAM_HEADER = struct.Struct("<8I")
UINT32_END = 1 << 32


def require(condition, message):
    if not condition:
        raise ValueError(message)


def power_of_two(value):
    return value > 0 and value & (value - 1) == 0


def load_plan(image, slot_base, slot_size, page_size=4096):
    """Return build metadata, not permission to load or execute the image.

    PT_LOAD defines initialized bytes followed by an exact zero-filled tail.
    p_paddr is deliberately unused: linker LMA is not runtime physical backing.
    """
    require(power_of_two(page_size) and page_size < UINT32_END,
            "page size must be a positive 32-bit power of two")
    require(0 <= slot_base < UINT32_END and
            0 < slot_size <= UINT32_END - slot_base, "invalid 32-bit slot range")
    require(slot_base % page_size == 0 and slot_size % page_size == 0,
            "slot base and size must be page aligned")
    require(len(image) >= ELF_HEADER.size, "truncated ELF header")
    (ident, kind, machine, version, entry, phoff, _shoff, flags, ehsize,
     phentsize, phnum, _shentsize, _shnum, _shstrndx) = ELF_HEADER.unpack_from(image)
    require(ident[:7] == b"\x7fELF\x01\x01\x01",
            "expected ELF32 little-endian version 1")
    require(kind == 2 and machine == 42 and version == 1,
            "expected a linked SH ET_EXEC image")
    require(ehsize == ELF_HEADER.size and phentsize == PROGRAM_HEADER.size,
            "unsupported ELF header sizes")
    require(0 < phnum < 0xffff, "missing or extended program-header count")
    require(phoff >= ehsize and phoff + phnum * phentsize <= len(image),
            "truncated program-header table")
    segments = []
    for i in range(phnum):
        (ptype, offset, address, _paddr, filesz, memsz,
         permissions, alignment) = PROGRAM_HEADER.unpack_from(image, phoff + i * phentsize)
        # Ignore only metadata with no loading action: NULL, NOTE, PHDR, STACK.
        if ptype in (0, 4, 6, 0x6474e551):
            continue
        require(ptype == 1, "unsupported program header (dynamic, TLS or other loading action)")
        require(filesz <= memsz, "segment file size exceeds memory size")
        require(offset <= len(image) and filesz <= len(image) - offset,
                "truncated load segment")
        require(alignment in (0, 1) or power_of_two(alignment),
                "invalid segment alignment")
        require(alignment <= 1 or address % alignment == offset % alignment,
                "incongruent segment file/address alignment")
        require(permissions & ~7 == 0, "unsupported segment permission flags")
        require(slot_base <= address <= slot_base + slot_size and
                memsz <= slot_base + slot_size - address,
                "segment outside execution slot")
        if not memsz:
            continue
        relative = address - slot_base
        require(not segments or relative >= segments[-1]["memory_offset"] +
                segments[-1]["memory_size"], "unordered or overlapping load segments")
        page_begin = relative // page_size * page_size
        page_end = (relative + memsz + page_size - 1) // page_size * page_size
        if segments and page_begin < segments[-1]["page_offset"] + segments[-1]["page_bytes"]:
            require(permissions == segments[-1]["elf_permissions"],
                    "different permissions share a page; separate them at link time")
        segments.append({
            "memory_offset": relative, "memory_size": memsz,
            "file_offset": offset, "file_size": filesz,
            "zero_offset": relative + filesz, "zero_size": memsz - filesz,
            "page_offset": page_begin, "page_bytes": page_end - page_begin,
            "elf_permissions": permissions, "alignment": alignment,
        })
    require(segments, "no nonempty load segments")
    require(entry % 2 == 0 and any(
        s["elf_permissions"] & 1 and
        slot_base + s["memory_offset"] <= entry and
        entry + 2 <= slot_base + s["memory_offset"] + s["file_size"]
        for s in segments), "entry is not a file-backed aligned instruction")
    return {
        "schema": "kos-overlay-load-plan", "version": 1,
        "image_sha256": hashlib.sha256(image).hexdigest(),
        "image_bytes": len(image), "elf_flags": flags,
        "elf_osabi": ident[7], "elf_abi_version": ident[8],
        "execution_base": slot_base, "slot_size": slot_size,
        "page_size": page_size, "entry_offset": entry - slot_base,
        "segments": segments,
    }


def integer(value):
    return int(value, 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--slot-base", type=integer, required=True)
    parser.add_argument("--slot-size", type=integer, required=True)
    parser.add_argument("--page-size", type=integer, default=4096)
    args = parser.parse_args()
    try:
        result = load_plan(args.image.read_bytes(), args.slot_base,
                           args.slot_size, args.page_size)
    except (OSError, ValueError) as error:
        parser.exit(1, f"elf-plan: {error}\n")
    json.dump(result, sys.stdout, sort_keys=True, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
