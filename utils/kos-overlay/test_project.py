"""Project-level generation and final-link refusal tests (no target execution)."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from dco_pack import SECTION, SYMBOL, pack
from elf_plan import ELF_HEADER
from project import project, layout, packages, resident_check, unique_pairs, write_new
from test_package import member
from test_plan import BASE


def config():
    return {"version": 1,
            "slots": [{"name": "game", "profile": "p1", "base": hex(BASE), "bytes": 8192}],
            "members": [{"name": "menu", "slot": "game", "elf": "menu.elf"},
                        {"name": "play", "slot": "game", "elf": "play.elf"}]}


def resident(base=BASE, size=8192, heap=None, kind=8, flags=3):
    """Original synthetic ELF section/symbol fixture, not executable code."""
    names = b"\0.text\0.dco_game\0.shstrtab\0.strtab\0.symtab\0"
    strings = b"\0_end\0"
    data = bytearray(ELF_HEADER.size)
    data.extend(b"\x09\x00")
    nameoff = len(data)
    data.extend(names)
    stroff = len(data)
    data.extend(strings)
    symoff = len(data)
    syms = bytes(SYMBOL.size) + SYMBOL.pack(1, heap if heap is not None else base + size,
                                          0, 0x10, 0, 0xfff1)
    data.extend(syms)
    sections = [(0,) * 10,
                (1, 1, 6, 0x8c010000, ELF_HEADER.size, 2, 0, 0, 2, 0),
                (names.index(b".dco_game"), kind, flags, base, 0, size, 0, 0, 32, 0),
                (names.index(b".shstrtab"), 3, 0, 0, nameoff, len(names), 0, 0, 1, 0),
                (names.index(b".strtab"), 3, 0, 0, stroff, len(strings), 0, 0, 1, 0),
                (names.index(b".symtab"), 2, 0, 0, symoff, len(syms), 4, 1, 4, SYMBOL.size)]
    shoff = len(data)
    for section in sections:
        data.extend(SECTION.pack(*section))
    ELF_HEADER.pack_into(data, 0, b"\x7fELF\x01\x01\x01" + bytes(9), 2, 42, 1,
                         0x8c010000, 0, shoff, 9, ELF_HEADER.size, 32, 0, SECTION.size, 6, 3)
    return bytes(data)


class ProjectTests(unittest.TestCase):
    def test_layout_deterministic_and_explicit(self):
        raw = config()
        raw["slots"].append(dict(name="worker", profile="vm", base="0x100000", bytes=8192))
        raw["members"].append(dict(name="logic", slot="worker", elf="logic.elf"))
        parsed = project(raw)
        generated = layout(parsed)
        self.assertIn(b".dco_game", generated["slots.ld"])
        self.assertNotIn(b".dco_worker", generated["slots.ld"])
        self.assertIn(b"0x00100000", generated["member_logic.ld"])
        raw["slots"].reverse()
        raw["members"].reverse()
        self.assertEqual(parsed, project(raw))
        self.assertEqual(generated, layout(project(raw)))

    def test_schema_rejections(self):
        variants = []
        for value in (2, True, "1"):
            raw = config(); raw["version"] = value; variants.append(raw)
        for field, value in (("name", "COUNT"), ("name", "count"), ("name", "x;evil"),
                             ("profile", "auto"), ("base", True), ("base", "0x8c100001"),
                             ("base", "0xac100000"), ("bytes", 0), ("bytes", 0x100020)):
            raw = config(); raw["slots"][0][field] = value; variants.append(raw)
        for field, value in (("slot", "missing"), ("elf", "/tmp/member.elf"),
                             ("elf", "../member.elf"), ("elf", "")):
            raw = config(); raw["members"][0][field] = value; variants.append(raw)
        raw = config(); raw["extra"] = 1; variants.append(raw)
        raw = config(); raw["slots"] = []; variants.append(raw)
        raw = config(); raw["members"] = []; variants.append(raw)
        raw = config(); raw["members"].append(raw["members"][0]); variants.append(raw)
        for raw in variants:
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                project(raw)
        with self.assertRaisesRegex(ValueError, "duplicate JSON"):
            json.loads('{"version":1,"version":1}', object_pairs_hook=unique_pairs)

    def test_overlap_and_unused_slots(self):
        for base in (BASE, BASE + 4096):
            raw = config()
            raw["slots"].append(dict(name="other", profile="p1", base=base, bytes=8192))
            with self.assertRaisesRegex(ValueError, "overlap"):
                project(raw)
        raw = config()
        raw["slots"].append(dict(name="other", profile="p1", base=BASE + 8192, bytes=8192))
        with self.assertRaisesRegex(ValueError, "no members"):
            project(raw)
        raw["members"].append(dict(name="other", slot="other", elf="other.elf"))
        project(raw)  # Adjacent ranges are allowed.

    def test_packages_reuse_exact_format(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for title in ("menu", "play"):
                (root / (title + ".elf")).write_bytes(member())
            result = packages(project(config()), root)
            self.assertEqual(result["member_menu.dco"], pack(member(), BASE, 8192)[0])
            report = json.loads(result["project.json"])
            self.assertFalse(report["resident_link_checked"])
            self.assertEqual(report["members"][0]["bss_bytes"], 4)
            self.assertEqual(report["members"][0]["tail_headroom_bytes"], 4088)
            self.assertIn(b"DCO_MEMBER_MENU", result["dco_project.h"])
            self.assertNotIn(str(root).encode(), result["project.json"])
            self.assertEqual(result, packages(project(config()), root))
            out = root / "out"
            write_new(out, result)
            with self.assertRaises(FileExistsError):
                write_new(out, result)
            self.assertEqual((out / "member_menu.dco").read_bytes(), result["member_menu.dco"])

    def test_vm_and_output_name_collisions(self):
        raw = config()
        raw["slots"][0].update(profile="vm", base=0x100000)
        raw["members"][0]["name"] = "project"
        raw["members"][1]["name"] = "slots"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for title in ("menu", "play"):
                (root / (title + ".elf")).write_bytes(member(0x100000))
            result = packages(project(raw), root)
            self.assertIn("member_project.json", result)
            self.assertIn("project.json", result)
            self.assertIn("member_slots.ld", layout(project(raw)))

    def test_member_refusals_leave_no_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "menu.elf").write_bytes(member(BASE + 8192))
            with self.assertRaises(ValueError):
                packages(project(config()), root)
            (root / "project.json").write_text(json.dumps(config()))
            run = subprocess.run([sys.executable, "-B", str(Path(__file__).with_name("project.py")),
                                  "package", root / "project.json", root / "out"], capture_output=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertFalse((root / "out").exists())

    def test_resident_bounds_and_layout(self):
        parsed = project(config())
        check = lambda image: resident_check(parsed, image, 0x8d000000, 65536, "_end")
        report = check(resident())
        self.assertEqual(report["checked_p1_slots"], ["game"])
        self.assertFalse(report["vm_backing_checked"])
        for image in (resident(size=4096), resident(base=BASE + 32), resident(kind=1),
                      resident(flags=7), resident(heap=BASE), resident(heap=0x8d000000),
                      resident()[:30]):
            with self.subTest(length=len(image)), self.assertRaises(ValueError):
                check(image)
        raw = bytearray(resident())
        shoff = ELF_HEADER.unpack_from(raw)[6]
        text = list(SECTION.unpack_from(raw, shoff + SECTION.size))
        text[3] = BASE  # Foreign allocated code overlapping an otherwise valid slot.
        SECTION.pack_into(raw, shoff + SECTION.size, *text)
        with self.assertRaisesRegex(ValueError, "overlapping"):
            check(bytes(raw))


if __name__ == "__main__":
    unittest.main()
