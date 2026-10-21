"""Build an original ISO9660/CUE fixture for the existing REIOS runtime.

Adapted from the original KOS resident-file disc fixture builder.
Requires macOS hdiutil and the built KOS scramble utility. The tiny original
bootstrap only jumps to the binary REIOS loaded; this is not a physical-console
boot/license bootstrap. The guest still mounts/reads actual /cd via direct I/O.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path, help='objcopy -O binary output')
parser.add_argument('output', type=Path, help='new output directory')
parser.add_argument('--scramble', type=Path, required=True)
args = parser.parse_args()
binary = args.binary.resolve()
scramble = args.scramble.resolve()
if not shutil.which('hdiutil'):
    parser.error('hdiutil is required; use a platform ISO mastering tool otherwise')
if not binary.is_file() or not scramble.is_file():
    parser.error('binary and built scramble executable must exist')
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
root = out / 'files'
root.mkdir()
names = ('A.DCO', 'B.DCO', 'BADHEAD.DCO', 'BADHASH.DCO')
for name in names:
    shutil.copyfile(binary.parent / name, root / name)
subprocess.run([str(scramble), str(binary), str(root / '1ST_READ.BIN')], check=True)
iso = out / 'disc.iso'
command = ['hdiutil', 'makehybrid', '-iso', '-default-volume-name', 'OVL2DISC',
           '-o', str(iso), str(root)]
subprocess.run(command, check=True)
# hdiutil omits ISO file version suffixes. Normalize this fixture root to
# standard ;1 names, as required by the runtime's ISO boot-file lookup.
image = bytearray(iso.read_bytes())
pvd = image[16 * 2048:17 * 2048]
assert pvd[:7] == b'\x01CD001\x01'
root_record = pvd[156:190]
root_start = struct.unpack_from('<I', root_record, 2)[0] * 2048
root_bytes = struct.unpack_from('<I', root_record, 10)[0]
assert root_bytes == 2048
records = bytearray()
cursor = 0
verified = set()
while cursor < root_bytes and image[root_start + cursor]:
    size = image[root_start + cursor]
    record = image[root_start + cursor:root_start + cursor + size]
    cursor += size
    length = record[32]
    name = record[33:33 + length]
    if not record[25] & 2:
        plain = bytes(name).split(b';', 1)[0].decode('ascii')
        start = struct.unpack_from('<I', record, 2)[0] * 2048
        count = struct.unpack_from('<I', record, 10)[0]
        assert start >= 32768 and start + count <= len(image)
        assert image[start:start + count] == (root / plain).read_bytes()
        assert plain not in verified
        verified.add(plain)
    if not record[25] & 2 and b';' not in name:
        extra = record[33 + length + (length % 2 == 0):]
        name += b';1'
        record = record[:33] + name + (b'\0' if len(name) % 2 == 0 else b'') + extra
        record[0] = len(record)
        record[32] = len(name)
    records.extend(record)
assert len(records) <= root_bytes
assert verified == {*names, '1ST_READ.BIN'}
image[root_start:root_start + root_bytes] = records.ljust(root_bytes, b'\0')
iso.write_bytes(image)
# ISO9660 reserves sectors 0..15 as the system area. Install original metadata
# and an original SH-4 jump stub there, leaving all filesystem sectors intact.
boot = bytearray(32768)
boot[:256] = b' ' * 256
for at, width, text in [(0, 16, b'SEGA SEGAKATANA'), (16, 16, b'OVL2 TEST'),
                        (32, 5, b'0000 '), (37, 6, b'CD-ROM'), (43, 5, b'1/1'),
                        (48, 8, b'JUE'), (56, 8, b'00001100'),
                        (64, 10, b'OVL2-TEST'), (74, 6, b'V1.000'),
                        (80, 8, b'20261020'), (96, 16, b'1ST_READ.BIN'),
                        (112, 16, b'KOS EXPERIMENT'), (128, 128, b'OVL2 DIRECT DISC')]:
    boot[at:at + width] = text.ljust(width, b' ')
# 0xac008300: mov.l @(1,pc),r0; jmp @r0; nop; nop; .long 0x8c010000
struct.pack_into('<HHHHI', boot, 0x300, 0xd001, 0x402b, 0x0009, 0x0009, 0x8c010000)
with iso.open('r+b') as image:
    image.write(boot)
cue = out / 'disc.cue'
cue.write_text('FILE "disc.iso" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n')
manifest = {'purpose': 'REIOS boot, actual ISO9660 /cd direct guest I/O; no hardware boot claim',
            'verified_iso_extents': sorted(verified),
            'iso_command': command,
            'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
            'files': {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in (*(root / name for name in names), root / '1ST_READ.BIN', iso, cue)}}
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(cue)
