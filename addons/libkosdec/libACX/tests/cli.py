"""Wire layout, payload, empty-member and collision checks for the ACX CLI."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

tool = Path(sys.argv[1]).resolve()


def check(root, *args, ok=True):
    run = subprocess.run([tool, *args], cwd=root, capture_output=True)
    assert (run.returncode == 0) == ok, run
    return run.stdout


def wire(data, rows, values, alignment):
    assert struct.unpack_from('>II', data) == (0, len(rows))
    assert [struct.unpack_from('>II', data, 8 + 8*i) for i in range(len(rows))] == rows
    tail = 8 + 8*len(rows)
    for (offset, size), value in zip(rows, values):
        assert offset % alignment == 0
        assert data[tail:offset] == bytes(offset-tail)
        assert data[offset:offset+size] == value
        tail = offset + size
    assert len(data) % alignment == 0 and data[tail:] == bytes(len(data)-tail)


with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    (root / 'one.bin').write_bytes(b'abc')
    (root / 'two.bin').write_bytes(b'defgh')
    (root / 'empty.bin').touch()
    check(root, 'pack', 'direct.acx', 'one.bin', 'two.bin')
    wire((root/'direct.acx').read_bytes(), [(24, 3), (28, 5)], [b'abc', b'defgh'], 4)

    (root / 'sample.fls').write_bytes(b'two.bin\r\nempty.bin\none.bin')
    check(root, 'pack-list', 'sample.fls')
    data = (root / 'sample.acx').read_bytes()
    wire(data, [(32, 5), (40, 0), (40, 3)], [b'defgh', b'', b'abc'], 4)
    assert check(root, 'list', 'sample.acx') == b'id\toffset\tbytes\n0\t32\t5\n1\t40\t0\n2\t40\t3\n'
    check(root, 'extract', 'sample.acx', 'members')
    assert [p.read_bytes() for p in sorted((root/'members').iterdir())] == [b'defgh', b'', b'abc']

    check(root, 'pack-list', 'sample.fls', 'wide.acx', '-al16')
    wire((root/'wide.acx').read_bytes(), [(32, 5), (48, 0), (48, 3)], [b'defgh', b'', b'abc'], 16)
    check(root, 'pack-list', 'sample.fls', '-al6', 'six.acx')
    wire((root/'six.acx').read_bytes(), [(36, 5), (42, 0), (42, 3)], [b'defgh', b'', b'abc'], 6)
    check(root, 'pack-list', 'sample.fls', 'bad.acx', '-al0', ok=False)
    check(root, 'pack-list', 'sample.fls', 'sample.acx', ok=False)
    assert (root / 'sample.acx').read_bytes() == data
    check(root, 'extract', 'sample.acx', 'members', ok=False)
    (root / 'empty.fls').touch()
    check(root, 'pack-list', 'empty.fls', ok=False)
    (root / 'bad.acx').write_bytes(data[:21])
    check(root, 'extract', 'bad.acx', 'bad', ok=False)
    assert not (root/'bad').exists()
print('ACX HOST TOOLS PASS')
