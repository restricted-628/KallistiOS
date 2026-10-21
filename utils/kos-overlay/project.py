#!/usr/bin/env python3
"""Generate explicit overlay project inputs; no compiler or runtime policy."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from dco_pack import SECTION, SYMBOL, PROFILE_P1, PROFILE_VM, pack, verify
from elf_plan import ELF_HEADER, load_plan, require


def fields(value, names, label):
    require(isinstance(value, dict) and set(value) == set(names),
            f"invalid {label} fields (expected {', '.join(names)})")


def number(value):
    require(type(value) is int or isinstance(value, str), "expected integer or hex string")
    result = int(value, 0) if isinstance(value, str) else value
    require(0 <= result < 1 << 32, "integer outside uint32 range")
    return result


def name(value):
    require(isinstance(value, str) and re.fullmatch(r"[a-z][a-z0-9_]{0,47}", value),
            "names must be lowercase identifiers, 1..48 characters")
    require(value != "count", "count is reserved for generated enum bounds")
    return value


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def project(raw):
    fields(raw, ("version", "slots", "members"), "project")
    require(type(raw["version"]) is int and raw["version"] == 1, "unsupported version")
    require(isinstance(raw["slots"], list) and raw["slots"], "slots must be a nonempty list")
    require(isinstance(raw["members"], list) and raw["members"], "members must be a nonempty list")
    slots, members = {}, {}
    for row in raw["slots"]:
        fields(row, ("name", "profile", "base", "bytes"), "slot")
        key = name(row["name"])
        require(key not in slots, "duplicate slot name")
        require(row["profile"] in ("p1", "vm"), "profile must be explicit p1 or vm")
        base, size = number(row["base"]), number(row["bytes"])
        direct = row["profile"] == "p1"
        granule = 32 if direct else 4096
        low, high = (0x8c000000, 0x8e000000) if direct else (0x1000, 0x7c000000)
        require(0 < size <= 0x100000 and low <= base < high and size <= high - base,
                "slot outside profile range or size budget")
        require(base % granule == 0 and size % granule == 0, "slot alignment")
        require(all(base + size <= s["base"] or s["base"] + s["bytes"] <= base
                    for s in slots.values()), "distinct slots overlap")
        slots[key] = dict(name=key, profile=row["profile"], base=base, bytes=size)
    for row in raw["members"]:
        fields(row, ("name", "slot", "elf"), "member")
        key, slot = name(row["name"]), name(row["slot"])
        require(key not in members, "duplicate member name")
        require(slot in slots, "unknown member slot")
        source = row["elf"]
        require(isinstance(source, str) and source and "\0" not in source,
                "ELF path must be a nonempty string")
        require(not Path(source).is_absolute() and ".." not in Path(source).parts,
                "ELF path must stay relative to project directory")
        members[key] = dict(name=key, slot=slot, elf=source)
    require({m["slot"] for m in members.values()} == set(slots), "slot has no members")
    return {"version": 1, "slots": sorted(slots.values(), key=lambda s: s["name"]),
            "members": sorted(members.values(), key=lambda m: m["name"])}


def layout(config):
    """Fragment is included inside resident SECTIONS, before its heap symbol."""
    slots = {s["name"]: s for s in config["slots"]}
    lines = ["/* Generated P1 reservations. INCLUDE inside SECTIONS before _end. */"]
    for slot in sorted(slots.values(), key=lambda s: s["base"]):
        if slot["profile"] == "p1":
            lines += [f'.dco_{slot["name"]} 0x{slot["base"]:08x} (NOLOAD) :',
                      f'{{ . += 0x{slot["bytes"]:x}; }}']
    result = {"slots.ld": ("\n".join(lines) + "\n").encode()}
    for member in config["members"]:
        slot = slots[member["slot"]]
        result["member_" + member["name"] + ".ld"] = (
            "/* Include before the application's member section script. */\n"
            f'SLOT_BASE = 0x{slot["base"]:08x};\n').encode()
    return result


def packages(config, root):
    slots = {s["name"]: (i, s) for i, s in enumerate(config["slots"])}
    outputs, rows, crows = {}, [], []
    for member in config["members"]:
        index, slot = slots[member["slot"]]
        image = (root / member["elf"]).read_bytes()
        profile = PROFILE_P1 if slot["profile"] == "p1" else PROFILE_VM
        data, identity = pack(image, slot["base"], slot["bytes"], profile)
        verify(data, identity)
        plan = load_plan(image, slot["base"], slot["bytes"], 32 if profile == PROFILE_P1 else 4096)
        spans = plan["segments"]
        used_end = max(s["memory_offset"] + s["memory_size"] for s in spans)
        filename = "member_" + member["name"] + ".dco"
        outputs[filename] = data
        outputs["member_" + member["name"] + ".json"] = json_bytes(identity)
        rows.append(dict(name=member["name"], slot=member["slot"], package=filename,
                         identity=identity, initialized_bytes=sum(s["file_size"] for s in spans),
                         bss_bytes=sum(s["zero_size"] for s in spans),
                         used_extent_bytes=used_end, tail_headroom_bytes=slot["bytes"] - used_end))
        digest = ",".join("0x" + identity["package_sha256"][i:i+2] for i in range(0, 64, 2))
        crows.append('    {%d, "%s", {%d, {%s}}}' % (index, filename, len(data), digest))
    header = ["/* Generated; one overlay project catalog per application. */",
              "#ifndef DCO_PROJECT_H", "#define DCO_PROJECT_H", "#include <kos/overlay.h>",
              "enum dco_slot_id {"]
    header += [f'    DCO_SLOT_{s["name"].upper()} = {i},' for i, s in enumerate(config["slots"])]
    header += [f'    DCO_SLOT_COUNT = {len(slots)}', "};", "enum dco_member_id {"]
    header += [f'    DCO_MEMBER_{m["name"].upper()} = {i},' for i, m in enumerate(config["members"])]
    header += [f'    DCO_MEMBER_COUNT = {len(rows)}', "};",
               "typedef struct dco_project_member {", "    unsigned slot;",
               "    const char *filename;", "    ovl_expected_t expected;", "} dco_project_member_t;",
               "extern const ovl_slot_options_t dco_project_slots[DCO_SLOT_COUNT];",
               "/* 1 = P1 constructor; 2 = explicit VM constructor. No automatic choice. */",
               "extern const unsigned dco_project_profiles[DCO_SLOT_COUNT];",
               "extern const dco_project_member_t dco_project_members[DCO_MEMBER_COUNT];", "#endif"]
    source = ['#include "dco_project.h"', "const ovl_slot_options_t dco_project_slots[DCO_SLOT_COUNT] = {"]
    source += [f'    {{.base = (void *)0x{s["base"]:08x}u, .bytes = {s["bytes"]}}},'
               for s in config["slots"]]
    source += ["};", "const unsigned dco_project_profiles[DCO_SLOT_COUNT] = {" +
               ",".join("1" if s["profile"] == "p1" else "2" for s in config["slots"]) + "};",
               "const dco_project_member_t dco_project_members[DCO_MEMBER_COUNT] = {",
               ",\n".join(crows), "};"]
    outputs["dco_project.h"] = ("\n".join(header) + "\n").encode()
    outputs["dco_project.c"] = ("\n".join(source) + "\n").encode()
    outputs["project.json"] = json_bytes(dict(version=1, slots=config["slots"], members=rows,
                                              resident_link_checked=False))
    return outputs


def resident_check(config, image, ram_end, stack_bytes, heap_symbol):
    """Check linked P1 reservations, not installed RAM or translated aliases."""
    require(0x8c000000 < ram_end <= 0x8e000000 and 0 < stack_bytes < ram_end - 0x8c000000,
            "invalid explicit RAM/stack budget")
    require(len(image) >= ELF_HEADER.size, "truncated resident ELF")
    h = ELF_HEADER.unpack_from(image)
    require(h[0][:7] == b"\x7fELF\x01\x01\x01" and h[1:4] == (2, 42, 1),
            "resident must be linked ELF32 little-endian SH")
    require(h[11] == SECTION.size and 0 < h[12] < 0xffff and 0 < h[13] < h[12] and
            h[6] >= ELF_HEADER.size and h[6] + h[12] * SECTION.size <= len(image),
            "invalid resident section table")
    sections = [SECTION.unpack_from(image, h[6] + i * SECTION.size) for i in range(h[12])]

    def data(s):
        require(s[4] <= len(image) and s[5] <= len(image) - s[4], "truncated resident section")
        return image[s[4]:s[4] + s[5]]

    def string(strings, offset):
        require(offset < len(strings) and b"\0" in strings[offset:], "invalid resident string")
        return strings[offset:strings.index(b"\0", offset)].decode("ascii")

    require(sections[h[13]][1] == 3, "invalid section names")
    names = data(sections[h[13]])
    named = [(string(names, s[0]), s) for s in sections]
    heaps = []
    for s in sections:
        if s[1] != 2:
            continue
        require(s[9] == SYMBOL.size and s[5] % SYMBOL.size == 0 and s[6] < len(sections)
                and sections[s[6]][1] == 3, "invalid resident symbols")
        strings = data(sections[s[6]])
        symbols = data(s)
        for offset in range(0, s[5], SYMBOL.size):
            sym = SYMBOL.unpack_from(symbols, offset)
            if string(strings, sym[0]) == heap_symbol and sym[5] != 0:
                heaps.append(sym[1])
    require(len(heaps) == 1, "expected one defined heap-start symbol")
    heap = heaps[0]
    require(0x8c000000 <= heap <= ram_end - stack_bytes, "heap overlaps reserved stack or exceeds RAM")
    for label, s in named:
        if not s[2] & 2 or not s[5]:
            continue
        require(s[3] + s[5] <= 1 << 32, "resident section wraps")
        # Only canonical P1 RAM is supported for this resident-link proof.
        require(0x8c000000 <= s[3] and s[3] + s[5] <= heap,
                "allocated resident section outside P1 RAM/heap boundary: " + label)
    checked = []
    for slot in config["slots"]:
        if slot["profile"] != "p1":
            continue
        base, end = slot["base"], slot["base"] + slot["bytes"]
        overlap = [(label, s) for label, s in named if s[2] & 2 and s[5] and
                   base < s[3] + s[5] and s[3] < end]
        require(len(overlap) == 1, "missing or overlapping resident slot: " + slot["name"])
        label, section = overlap[0]
        require(label == ".dco_" + slot["name"] and section[1] == 8 and
                section[2] == 3 and section[3] == base and section[5] == slot["bytes"],
                "slot must exactly match its dedicated writable NOBITS reservation")
        checked.append(slot["name"])
    return dict(version=1, resident_sha256=hashlib.sha256(image).hexdigest(),
                heap_start=heap, ram_end=ram_end, stack_bytes=stack_bytes,
                checked_p1_slots=checked, vm_backing_checked=False)


def json_bytes(value):
    return (json.dumps(value, sort_keys=True, indent=2) + "\n").encode()


def write_new(directory, outputs):
    # All semantic validation precedes creation. An I/O failure may leave a
    # partial directory; only successful command completion permits publication.
    directory.mkdir(parents=True, exist_ok=False)
    for filename, content in outputs.items():
        with (directory / filename).open("xb") as output:
            output.write(content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("layout", "package"):
        p = sub.add_parser(command)
        p.add_argument("project", type=Path)
        p.add_argument("output", type=Path)
    p = sub.add_parser("check-resident")
    p.add_argument("project", type=Path)
    p.add_argument("resident", type=Path)
    p.add_argument("--ram-end", type=number, required=True)
    p.add_argument("--stack-bytes", type=number, required=True)
    p.add_argument("--heap-symbol", default="_end")
    args = parser.parse_args()
    try:
        config = project(json.loads(args.project.read_text(), object_pairs_hook=unique_pairs))
        if args.command == "check-resident":
            report = resident_check(config, args.resident.read_bytes(), args.ram_end,
                                    args.stack_bytes, args.heap_symbol)
            sys.stdout.buffer.write(json_bytes(report))
        else:
            outputs = layout(config) if args.command == "layout" else packages(config, args.project.parent)
            write_new(args.output, outputs)
    except (OSError, ValueError, TypeError) as error:
        parser.exit(1, f"overlay-project: {error}\n")


if __name__ == "__main__":
    main()
