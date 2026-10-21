"""DCo@ serializer and restricted ELF admission; no execution/hardware claims."""
import hashlib
import struct
import unittest

from dco_pack import HEADER, DESCRIPTOR, SECTION, SYMBOL, pack, verify
from elf_plan import ELF_HEADER
from test_plan import BASE, image


def member():
    data = bytearray(image([(1, 0x100, BASE, 0, 4, 4, 5, 4),
                            (1, 0x104, BASE + 4096, 0, 4, 8, 6, 4)]))
    names = b"\0.text\0.data\0.bss\0.shstrtab\0.strtab\0.symtab\0"
    strings = b"\0member.c\0_overlay_entry\0_state\0SLOT_BASE\0"
    syms = bytes(16) + SYMBOL.pack(1, 0, 0, 4, 0, 0xfff1)
    syms += SYMBOL.pack(strings.index(b"_overlay_entry"), BASE, 4, 0x12, 0, 1)
    syms += SYMBOL.pack(strings.index(b"_state"), BASE + 4100, 4, 0x11, 0, 3)
    syms += SYMBOL.pack(strings.index(b"SLOT_BASE"), BASE, 0, 0x10, 0, 0xfff1)
    nameoff = len(data)
    data.extend(names)
    stroff = len(data)
    data.extend(strings)
    data.extend(bytes((-len(data)) % 4))
    symoff = len(data)
    data.extend(syms)
    shoff = len(data)
    sections = [(0,) * 10,
                (names.index(b".text"), 1, 6, BASE, 0x100, 4, 0, 0, 4, 0),
                (names.index(b".data"), 1, 3, BASE + 4096, 0x104, 4, 0, 0, 4, 0),
                (names.index(b".bss"), 8, 3, BASE + 4100, 0x108, 4, 0, 0, 4, 0),
                (names.index(b".shstrtab"), 3, 0, 0, nameoff, len(names), 0, 0, 1, 0),
                (names.index(b".strtab"), 3, 0, 0, stroff, len(strings), 0, 0, 1, 0),
                (names.index(b".symtab"), 2, 0, 0, symoff, len(syms), 5, 2, 4, 16)]
    for section in sections:
        data.extend(SECTION.pack(*section))
    h = list(ELF_HEADER.unpack_from(data))
    h[6], h[11], h[12], h[13] = shoff, SECTION.size, len(sections), 4
    ELF_HEADER.pack_into(data, 0, *h)
    return bytes(data)


def section_change(data, index, field, value):
    result = bytearray(data)
    offset = ELF_HEADER.unpack_from(data)[6] + index * SECTION.size
    struct.pack_into("<I", result, offset + field * 4, value)
    return bytes(result)


class PackageTests(unittest.TestCase):
    def test_layout_identity_and_roundtrip(self):
        elf = member()
        package, manifest = pack(elf, BASE, 8192)
        self.assertEqual(HEADER.size, 64)
        self.assertEqual(DESCRIPTOR.size, 32)
        self.assertEqual(package[:8], b"DCo@\x01\x00\x40\x00")
        self.assertEqual(package[128:], elf)
        self.assertEqual(manifest["elf_sha256"], hashlib.sha256(elf).hexdigest())
        self.assertNotEqual(manifest["elf_sha256"], manifest["package_sha256"])
        self.assertEqual(verify(package, manifest), manifest)
        self.assertEqual(pack(elf, BASE, 8192), (package, manifest))
        self.assertEqual(DESCRIPTOR.unpack_from(package, 96)[:4], (4096, 4, 8, 0x104))

    def test_payload_and_nonloadable_identity(self):
        package, manifest = pack(member(), BASE, 8192)
        for offset in (0, 8, 48, 88, 128 + 0x100, 128 + 0x180, len(package) - 1):
            bad = bytearray(package)
            bad[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, "digest"):
                verify(bytes(bad), manifest)
        for bad in (package[:-1], package + b"\0"):
            with self.assertRaises(ValueError):
                verify(bad, manifest)

    def test_metadata_rejected_even_with_matching_digest(self):
        package, manifest = pack(member(), BASE, 8192)
        for offset in (0, 4, 6, 8, 12, 24, 28, 32, 36, 40, 44, 48, 64, 88):
            bad = bytearray(package)
            bad[offset] ^= 1
            trusted = dict(manifest, package_sha256=hashlib.sha256(bad).hexdigest())
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                verify(bytes(bad), trusted)

    def test_profile_rejects_stripped_or_wrong_machine_flags(self):
        elf = member()
        for field, value in ((6, 0), (7, 12), (7, 0x109), (11, 0), (12, 0), (13, 0)):
            bad = bytearray(elf)
            h = list(ELF_HEADER.unpack_from(bad))
            h[field] = value
            ELF_HEADER.pack_into(bad, 0, *h)
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                pack(bytes(bad), BASE, 8192)

    def test_unsupported_sections(self):
        elf = member()
        for kind in (4, 6, 9, 11, 14, 15, 16, 17):
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                pack(section_change(elf, 2, 1, kind), BASE, 8192)
        for index, field, value in ((2, 2, 0x403), (2, 4, 0x108),
                                    (3, 3, BASE + 4096), (2, 5, 0xffffffff),
                                    (2, 0, 0xffffffff), (2, 8, 3),
                                    (6, 6, 7), (6, 7, 0), (6, 9, 0)):
            with self.subTest(index=index, field=field), self.assertRaises(ValueError):
                pack(section_change(elf, index, field, value), BASE, 8192)

    def test_unknown_allocated_name(self):
        elf = member().replace(b".data\0", b".init\0")
        with self.assertRaisesRegex(ValueError, "allocated section"):
            pack(elf, BASE, 8192)

    def test_undefined_weak_tls_common_or_wrong_entry(self):
        elf = member()
        shoff = ELF_HEADER.unpack_from(elf)[6]
        symoff = SECTION.unpack_from(elf, shoff + 6 * SECTION.size)[4]
        for field, value in ((0, 0xffffffff), (1, BASE + 2), (2, 0),
                             (3, 0x22), (3, 0x16), (4, 2), (5, 0), (5, 0xfff2)):
            bad = bytearray(elf)
            at = symoff + 2 * SYMBOL.size
            sym = list(SYMBOL.unpack_from(bad, at))
            sym[field] = value
            SYMBOL.pack_into(bad, at, *sym)
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                pack(bytes(bad), BASE, 8192)

    def test_arbitrary_absolute_import(self):
        elf = member().replace(b"SLOT_BASE\0", b"HOST_FUNC\0")
        with self.assertRaisesRegex(ValueError, "absolute symbol"):
            pack(elf, BASE, 8192)

    def test_slot_and_source_limits(self):
        for base, size in ((0x00100000, 8192), (BASE + 1, 8192),
                            (BASE, 8193), (BASE, 0x100020), (0x8dffffe0, 64)):
            with self.subTest(base=base, size=size), self.assertRaises(ValueError):
                pack(member(), base, size)
        for cut in (0, 51, 100, len(member()) - 1):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                pack(member()[:cut], BASE, 8192)


if __name__ == "__main__":
    unittest.main()
