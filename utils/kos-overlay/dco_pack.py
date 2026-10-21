#!/usr/bin/env python3
"""Build/verify DCo@ v1 packages for the restricted static-C overlay profile.

This is build-time admission for trusted code, not an instruction verifier,
runtime loader, signature scheme or proof of application ABI compatibility.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from elf_plan import ELF_HEADER, integer, load_plan, power_of_two, require

HEADER = struct.Struct("<4sHH10I16s")
DESCRIPTOR = struct.Struct("<6I8s")
SECTION = struct.Struct("<10I")
SYMBOL = struct.Struct("<IIIBBH")
MAX_SLOT = 0x100000
MAX_SEGMENTS = 16
INT32_MAX = 0x7fffffff


def cstring(table, offset):
    require(0 <= offset < len(table), "string offset outside table")
    end = table.find(b"\0", offset)
    require(end >= 0, "unterminated ELF string")
    try:
        return table[offset:end].decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("non-ASCII ELF name in static-C profile") from error


def static_c_plan(image, base, size):
    """Narrow unstripped GNU SH4 ELF profile; no runtime import/relocation action."""
    require(0x8c000000 <= base < 0x8e000000 and
            0 < size <= MAX_SLOT and size <= 0x8e000000 - base,
            "expected bounded P1 system-RAM candidate slot")
    # 32 is a host coverage granule, not an MMU page-size assertion.
    plan = load_plan(image, base, size, 32)
    require(plan["elf_flags"] == 9 and plan["elf_osabi"] == 0 and
            plan["elf_abi_version"] == 0, "expected plain GNU SH4 ELF flags/ABI")
    require(len(plan["segments"]) <= MAX_SEGMENTS, "too many load segments")
    require(all(s["elf_permissions"] in (4, 5, 6) for s in plan["segments"]),
            "expected readable non-WX load segments")
    fields = ELF_HEADER.unpack_from(image)
    shoff, entsize, count, names_index = fields[6], fields[11], fields[12], fields[13]
    require(entsize == SECTION.size and 1 < count < 0xff00 and
            0 < names_index < count, "missing/extended section metadata")
    require(shoff >= ELF_HEADER.size and shoff + count * entsize <= len(image),
            "truncated section table")
    sections = [SECTION.unpack_from(image, shoff + i * entsize) for i in range(count)]
    require(sections[0] == (0,) * 10, "invalid null section")

    def data(section):
        offset, length = section[4:6]
        require(offset <= len(image) and length <= len(image) - offset,
                "section outside file")
        return image[offset:offset + length]

    require(sections[names_index][1:3] == (3, 0), "invalid section name table")
    names = data(sections[names_index])
    require(names.startswith(b"\0"), "invalid section names")
    symtabs, allocated = [], []
    for i, s in enumerate(sections[1:], 1):
        name = cstring(names, s[0])
        kind, flags, addr, offset, length, link, info, align, stride = s[1:]
        require(align in (0, 1) or power_of_two(align), "invalid section alignment")
        if kind != 8:
            data(s)
        require(flags & ~7 == 0, "unsupported section flags (including TLS)")
        if flags & 2:
            families = ((".text", 1, 6), (".rodata", 1, 2),
                        (".data", 1, 3), (".bss", 8, 3))
            require(any((name == n or name.startswith(n + ".")) and
                        kind == k and flags == f for n, k, f in families),
                    "unsupported allocated section: " + name)
            require(link == 0 and info == 0 and stride == 0,
                    "unsupported allocated-section metadata")
            require(not length or align <= 1 or addr % align == 0,
                    "misaligned allocated section")
            if length:
                relative = addr - base
                owners = [p for p in plan["segments"] if
                          p["memory_offset"] <= relative and
                          relative + length <= p["memory_offset"] + p["memory_size"]]
                require(len(owners) == 1, "section not covered by one load segment")
                p = owners[0]
                require(not flags & 1 or p["elf_permissions"] & 2,
                        "writable section in non-writable segment")
                require(not flags & 4 or p["elf_permissions"] & 1,
                        "code section in non-executable segment")
                if kind == 8:
                    require(relative >= p["zero_offset"], "BSS overlaps file-backed data")
                else:
                    delta = relative - p["memory_offset"]
                    require(delta + length <= p["file_size"] and
                            offset == p["file_offset"] + delta,
                            "section/file mapping disagrees with load segment")
                allocated.append((addr, addr + length))
        else:
            require(flags == 0, "non-allocated section has active flags")
            require((kind == 2 and name == ".symtab") or
                    (kind == 3 and name in (".strtab", ".shstrtab")) or
                    (kind == 1 and (name == ".comment" or name.startswith(".debug"))),
                    "unsupported metadata/relocation section: " + name)
            if kind == 2:
                symtabs.append(s)
    ordered = sorted(allocated)
    require(all(a[1] <= b[0] for a, b in zip(ordered, ordered[1:])),
            "overlapping allocated sections")
    require(len(symtabs) == 1, "one complete symbol table required (do not strip)")
    table = symtabs[0]
    require(table[9] == SYMBOL.size and table[5] % SYMBOL.size == 0 and
            0 < table[6] < count and sections[table[6]][1:3] == (3, 0),
            "invalid symbol table")
    strings = data(sections[table[6]])
    require(strings.startswith(b"\0"), "invalid symbol string table")
    symbols = data(table)
    require(len(symbols) >= SYMBOL.size and symbols[:SYMBOL.size] == bytes(SYMBOL.size),
            "invalid null symbol")
    symbol_count = len(symbols) // SYMBOL.size
    require(0 < table[7] <= symbol_count, "invalid local-symbol boundary")
    entry_found = False
    for i in range(1, symbol_count):
        name_at, value, length, info, other, index = SYMBOL.unpack_from(symbols, i * SYMBOL.size)
        name = cstring(strings, name_at)
        binding, kind = info >> 4, info & 15
        require(binding in (0, 1) and kind in (0, 1, 2, 3, 4) and other == 0,
                "unsupported weak/TLS/other symbol")
        require((i < table[7]) == (binding == 0), "invalid symbol binding order")
        require(index != 0, "unresolved symbol: " + name)
        if kind == 4:
            require(binding == 0 and index == 0xfff1 and value == 0 and length == 0,
                    "invalid file symbol")
            continue
        if index == 0xfff1:
            require(name == "SLOT_BASE" and kind == 0 and value == base and length == 0,
                    "unsupported absolute symbol: " + name)
            continue
        require(0 < index < count, "unsupported common/extended symbol index")
        owner = sections[index]
        require(owner[2] & 2 and owner[3] <= value <= owner[3] + owner[5] and
                length <= owner[3] + owner[5] - value, "symbol outside allocated section")
        if kind == 2:
            require(owner[2] & 4 and value % 2 == 0, "function outside aligned code")
        if name == "_overlay_entry":
            require(binding == 1 and kind == 2 and value == base + plan["entry_offset"] and
                    length >= 2 and not entry_found, "invalid overlay entry symbol")
            entry_found = True
    require(entry_found, "missing typed _overlay_entry")
    return plan


def pack(image, base, size):
    plan = static_c_plan(image, base, size)
    segments = plan["segments"]
    elf_offset = HEADER.size + len(segments) * DESCRIPTOR.size
    total = elf_offset + len(image)
    require(total <= INT32_MAX, "package exceeds supported source length")
    header = HEADER.pack(b"DCo@", 1, HEADER.size, 0, len(segments), base, size,
                         plan["entry_offset"], 1, elf_offset, len(image), total,
                         DESCRIPTOR.size, bytes(16))
    descriptors = b"".join(DESCRIPTOR.pack(s["memory_offset"], s["file_size"],
                           s["memory_size"], s["file_offset"], s["elf_permissions"],
                           s["alignment"], bytes(8)) for s in segments)
    package = header + descriptors + image
    manifest = {"schema": "kos-overlay-package", "version": 1,
                "package_bytes": total, "package_sha256": hashlib.sha256(package).hexdigest(),
                "elf_bytes": len(image), "elf_sha256": plan["image_sha256"],
                "execution_base": base, "slot_bytes": size,
                "entry_offset": plan["entry_offset"], "abi_profile": 1}
    return package, manifest


def verify(package, expected):
    require(expected.get("schema") == "kos-overlay-package" and expected.get("version") == 1,
            "unsupported expected manifest")
    require(len(package) >= HEADER.size and len(package) == expected.get("package_bytes") and
            hashlib.sha256(package).hexdigest() == expected.get("package_sha256"),
            "package length/digest mismatch")
    h = HEADER.unpack_from(package)
    require(h[0:4] == (b"DCo@", 1, HEADER.size, 0) and
            1 <= h[4] <= MAX_SEGMENTS and h[8] == 1 and
            h[9] == HEADER.size + h[4] * DESCRIPTOR.size and
            h[10] > 0 and h[9] + h[10] == h[11] == len(package) and
            h[12] == DESCRIPTOR.size and h[13] == bytes(16), "invalid package header")
    rebuilt, manifest = pack(package[h[9]:], h[5], h[6])
    require(rebuilt == package, "package metadata disagrees with admitted ELF")
    require(manifest == expected, "expected manifest disagrees with package")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("pack")
    make.add_argument("image", type=Path)
    make.add_argument("package", type=Path)
    make.add_argument("manifest", type=Path)
    make.add_argument("--slot-base", type=integer, required=True)
    make.add_argument("--slot-size", type=integer, required=True)
    check = commands.add_parser("verify")
    check.add_argument("package", type=Path)
    check.add_argument("manifest", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "pack":
            package, manifest = pack(args.image.read_bytes(), args.slot_base, args.slot_size)
            verify(package, manifest)
            require(args.package.resolve() != args.manifest.resolve(), "outputs must differ")
            require(not args.package.exists() and not args.manifest.exists(),
                    "output already exists (choose fresh paths)")
            # Exclusive creation also protects against overwriting a racing producer.
            # A failed second write can leave the package; never publish a partial pair.
            with args.package.open("xb") as output:
                output.write(package)
            with args.manifest.open("x") as output:
                json.dump(manifest, output, sort_keys=True, indent=2)
                output.write("\n")
        else:
            expected = json.loads(args.manifest.read_text())
            require(isinstance(expected, dict), "expected manifest object")
            manifest = verify(args.package.read_bytes(), expected)
        print(json.dumps(manifest, sort_keys=True))
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"dco-pack: {error}\n")


if __name__ == "__main__":
    main()
