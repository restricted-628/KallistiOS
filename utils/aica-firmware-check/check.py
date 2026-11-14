#!/usr/bin/env python3
"""Explicit static check of the current private ARM firmware; no device access."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tempfile


def require(condition, message):
    if not condition:
        raise ValueError(message)


class Elf:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        d = self.data
        require(d[:6] == b"\x7fELF\x01\x01" and
                struct.unpack_from("<H", d, 18)[0] == 40, "expected ELF32LE ARM")
        self.entry = struct.unpack_from("<I", d, 24)[0]
        off = struct.unpack_from("<I", d, 32)[0]
        size, count, names = struct.unpack_from("<HHH", d, 46)
        require(size == 40 and off + count * size <= len(d), "section table")
        self.sections = [struct.unpack_from("<10I", d, off + i * size)
                         for i in range(count)]
        table = self.section_bytes(self.sections[names])
        self.named = {self.string(table, s[0]): s for s in self.sections}
        self.section_names = [self.string(table, s[0]) for s in self.sections]
        self.symbols = []
        self.symbol_tables = {}
        for section_index, section in enumerate(self.sections):
            if section[1] != 2:
                continue
            strings = self.section_bytes(self.sections[section[6]])
            require(section[9] == 16, "symbol size")
            symbols = []
            for offset in range(section[4], section[4] + section[5], 16):
                name, value, length, info, other, index = struct.unpack_from("<IIIBBH", d, offset)
                self.symbols.append((self.string(strings, name), value, length, info, index))
                target_section = self.section_names[index] if index < len(self.sections) else index
                symbols.append((self.string(strings, name), value, length, info, other, target_section))
            self.symbol_tables[section_index] = symbols

    @staticmethod
    def string(data, offset):
        return data[offset:data.index(b"\0", offset)].decode()

    def section_bytes(self, section):
        offset, size = section[4:6]
        require(offset + size <= len(self.data), "section extent")
        return self.data[offset:offset + size]

    def symbol(self, name):
        matches = [s for s in self.symbols if s[0] == name and s[4]]
        require(len(matches) == 1, "missing/ambiguous symbol " + name)
        return matches[0]

    def image(self):
        off = struct.unpack_from("<I", self.data, 28)[0]
        size, count = struct.unpack_from("<HH", self.data, 42)
        require(size == 32, "program header size")
        loads = [struct.unpack_from("<8I", self.data, off + i * size) for i in range(count)]
        loads = [s for s in loads if s[0] == 1]
        require(loads and max(s[3] + s[5] for s in loads) <= 0x10000, "LOAD exceeds program region")
        image = bytearray(max(s[3] + s[4] for s in loads if s[4]))
        for _, offset, virtual, physical, length, memory, flags, alignment in loads:
            require(virtual == physical and length <= memory and offset + length <= len(self.data), "LOAD geometry")
            image[physical:physical + length] = self.data[offset:offset + length]
        return bytes(image)

    def allocated(self):
        result = {}
        for name, s in self.named.items():
            if s[1] in (4, 9):
                # r_info contains an index, not the identity of its target.
                # Resolve it through this relocation section's linked symtab.
                width = 12 if s[1] == 4 else 8
                require(s[9] == width and s[5] % width == 0, "relocation size")
                targets = self.symbol_tables[s[6]]
                relocations = []
                for offset in range(s[4], s[4] + s[5], width):
                    at, info = struct.unpack_from("<II", self.data, offset)
                    addend = struct.unpack_from("<i", self.data, offset + 8)[0] if width == 12 else None
                    relocations.append((at, info & 255, targets[info >> 8], addend))
                result[name] = (s[1], self.section_names[s[7]], tuple(relocations))
            elif s[2] & 2:
                result[name] = (s[1], s[2], s[5], b"" if s[1] == 8 else self.section_bytes(s))
        return result


def helper_hashes(elf, image):
    functions = sorted(set(s[1] for s in elf.symbols if s[3] & 15 == 2 and s[4]))
    result = {}
    for name, address, size, info, index in elf.symbols:
        if not name.startswith("__") or info & 15 != 2 or not index:
            continue
        if not size:
            size = next(x for x in functions if x > address) - address
        result[name] = hashlib.sha256(image[address:address + size]).hexdigest()
    return result


def check(args):
    root = Path(__file__).resolve().parents[2]
    arm = root / "kernel/arch/dreamcast/sound/arm"
    native = root / "utils/aica-sequence-tool/native"
    elf = Elf(arm / "prog.elf")
    image = elf.image()
    require(elf.entry == 0 and image == (arm / "stream.drv").read_bytes(), "entry/raw image mismatch")
    require(not [s for s in elf.symbols if s[0] and not s[4] and s[3] >> 4], "undefined symbols")
    start = [s for s in elf.symbols if s[0] == "start" and s[3] & 15 == 0]
    require(len(start) == 1 and struct.unpack_from("<I", image, start[0][1])[0] == 0xe3a0d801,
            "review startup SP; expected mov sp,#0x10000")
    expected = json.loads(Path(__file__).with_name("helpers.json").read_text())
    require(helper_hashes(elf, image) == expected, "helper implementation changed; review ISA/call-chain/stack before updating helpers.json")
    tool = str(Path(args.cc).with_name(Path(args.cc).name.replace("gcc", "objdump")))
    disassembly = subprocess.check_output([tool, "-d", str(arm / "prog.elf")], text=True)
    require(not re.search(r"\t(?:blx|bx|clz|ldrh|ldrsh|ldrsb|strh|umull|smull|smlal|umlal)\s", disassembly),
            "unsupported ARM7DI instruction family")
    frames = []
    with tempfile.TemporaryDirectory(prefix="aica-layout-") as temporary:
        temporary = Path(temporary)
        common = [args.cc, *shlex.split(args.flags), *shlex.split(args.includes),
                  "-I.", "-I" + str(native), "-I" + str(root / "kernel/arch/dreamcast/include"),
                  "-I" + str(root / "kernel/arch/dreamcast/include/dc/sound")]
        mapping = {"asq_native.o": native / "asq.c", "asq_catalog.o": native / "catalog.c",
                   "asq_wire.o": native / "wire.c", "asq_live.o": native / "live.c"}
        require(args.objects and len(args.objects) == len(set(args.objects)), "object inventory")
        for name in args.objects:
            require(Path(name).name == name and name.endswith(".o"), "object name")
            source = mapping.get(name, Path(name).with_suffix(".c"))
            output = temporary / name
            command = [*common, "-fstack-usage", "-Wall", "-Wextra", "-Werror", "-c", str(source), "-o", str(output)]
            subprocess.run(command, cwd=arm, check=True)
            require(Elf(output).allocated() == Elf(arm / name).allocated(), "stack probe differs from built object " + name)
            for line in output.with_suffix(".su").read_text().splitlines():
                function, size, kind = line.split("\t")
                require(kind == "static", "nonstatic stack: " + function)
                frames.append((function, int(size)))
        # Included headers assert actual ARM receipt/state separation.
        probe = temporary / "layout.c"
        probe.write_text('#include "aica_sequence_resource.h"\n'
                         'char sequence_extent[sizeof(asq_resource_runtime_t)];\n'
                         'char music_extent[sizeof(aica_music_live_t)];\n')
        subprocess.run([*common, "-c", str(probe), "-o", str(temporary / "layout.o")], cwd=arm, check=True)
        layout = Elf(temporary / "layout.o")
        sequence, music = layout.symbol("sequence_extent")[2], layout.symbol("music_extent")[2]
    total, end, reserve = sum(size for name, size in frames), elf.symbol("_end")[1], 512
    require(frames and end + total + reserve < 0x10000, "image/BSS plus static stack bound collides with stack top")
    require(0x2a000 + music <= 0x2f000 and 0x2f000 + sequence <= 0x30000, "private state overlap")
    return {"image_bytes": len(image), "image_sha256": hashlib.sha256(image).hexdigest(),
            "end": end, "stack_top": 65536, "all_c_frames": total, "helper_reserve": reserve,
            "headroom": 65536 - end - total - reserve, "music_bytes": music, "sequence_bytes": sequence}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--flags", default="")
    parser.add_argument("--includes", default="")
    parser.add_argument("objects", nargs="+")
    try:
        print(json.dumps(check(parser.parse_args()), indent=2))
    except (ValueError, OSError, struct.error, subprocess.CalledProcessError) as error:
        parser.exit(1, "AICA layout check FAILED: " + str(error) + "\n")
