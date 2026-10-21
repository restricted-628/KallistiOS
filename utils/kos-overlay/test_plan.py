"""Focused format tests; no target execution or memory-mapping simulation."""
import unittest

from elf_plan import ELF_HEADER, PROGRAM_HEADER, load_plan

BASE = 0x8c100000


def image(headers=None, entry=BASE, **changes):
    if headers is None:
        headers = [(1, 0x100, BASE, 0x12340000, 4, 8, 5, 4)]
    fields = dict(ident=b"\x7fELF\x01\x01\x01" + bytes(9), kind=2, machine=42,
                  version=1, entry=entry, phoff=52, shoff=0, flags=9, ehsize=52,
                  phentsize=32, phnum=len(headers), shentsize=0, shnum=0, shstrndx=0)
    fields.update(changes)
    result = bytearray(0x200)
    result[:52] = ELF_HEADER.pack(*fields.values())
    for i, header in enumerate(headers):
        PROGRAM_HEADER.pack_into(result, 52 + i * 32, *header)
    result[0x100:0x104] = b"\x0b\x00\x09\x00"  # rts; nop
    return bytes(result)


class PlanTests(unittest.TestCase):
    def plan(self, data, base=BASE, size=0x2000, page=4096):
        return load_plan(data, base, size, page)

    def test_exact_zero_tail_and_no_physical_address(self):
        p = self.plan(image())
        self.assertEqual(p["segments"][0]["zero_offset"], 4)
        self.assertEqual(p["segments"][0]["zero_size"], 4)
        self.assertNotIn("physical_address", p["segments"][0])
        self.assertEqual(p, self.plan(image()))

    def test_translated_vma(self):
        base = 0x100000
        p = self.plan(image([(1, 0x100, base, BASE, 4, 8, 5, 4)], base), base)
        self.assertEqual(p["execution_base"], base)
        self.assertEqual(p["entry_offset"], 0)

    def test_split_segments_and_zero_only_bss(self):
        p = self.plan(image([(1, 0x100, BASE, 0, 4, 4, 5, 4),
                             (1, 0x104, BASE + 4096, 0, 0, 32, 6, 4)]))
        self.assertEqual(p["segments"][1]["zero_offset"], 4096)
        self.assertEqual(p["segments"][1]["file_size"], 0)

    def test_truncated_headers_and_payload(self):
        for end in (0, 51, 52, 83, 259):
            with self.subTest(end=end), self.assertRaises(ValueError):
                self.plan(image()[:end])

    def test_bad_elf_headers(self):
        for changes in ({"kind": 1}, {"kind": 3}, {"machine": 8}, {"version": 2},
                        {"ehsize": 0}, {"phentsize": 0}, {"phoff": 0},
                        {"phnum": 0xffff}, {"phnum": 0},
                        {"ident": b"\x7fELF\x02\x01\x01" + bytes(9)}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.plan(image(**changes))

    def test_bad_segments(self):
        good = [1, 0x100, BASE, 0, 4, 8, 5, 4]
        for field, value in ((0, 2), (0, 3), (0, 7), (0, 0x70000000),
                             (1, 0x1ff), (2, BASE - 4), (2, BASE + 8192),
                             (4, 9), (5, 0xffffffff), (6, 8), (7, 3), (7, 512)):
            bad = good.copy()
            bad[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                self.plan(image([bad]))

    def test_bad_entry(self):
        for entry in (BASE + 1, BASE + 4, BASE + 4096):
            with self.subTest(entry=entry), self.assertRaises(ValueError):
                self.plan(image(entry=entry))
        with self.assertRaises(ValueError):
            self.plan(image([(1, 0x100, BASE, 0, 4, 8, 6, 4)]))

    def test_overlap_and_page_permission_conflict(self):
        first = (1, 0x100, BASE, 0, 4, 8, 5, 4)
        for second in ((1, 0x104, BASE + 4, 0, 4, 4, 5, 4),
                       (1, 0x108, BASE + 8, 0, 4, 4, 6, 4)):
            with self.subTest(second=second), self.assertRaises(ValueError):
                self.plan(image([first, second]))

    def test_invalid_slot(self):
        for base, size, page in ((BASE, 0, 4096), (BASE + 1, 4096, 4096),
                                  (BASE, 4097, 4096), (BASE, 4096, 3),
                                  (-4096, 4096, 4096), (0xfffff000, 8192, 4096)):
            with self.subTest(base=base, size=size, page=page), self.assertRaises(ValueError):
                self.plan(image(), base, size, page)


if __name__ == "__main__":
    unittest.main()
