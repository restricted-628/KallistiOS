"""Independent structural/wire checks and payload round trips for the host tool."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def run(tool, *args, ok=True):
    result = subprocess.run([tool, *map(str, args)], capture_output=True)
    if (result.returncode == 0) != ok:
        raise AssertionError(result)
    return result.stdout


def entries(data):
    magic, count = struct.unpack_from('<4sI', data)
    assert magic == b'AFS\0'
    pairs = [struct.unpack_from('<II', data, 8 + n * 8) for n in range(count)]
    pointer = struct.unpack_from('<II', data, 8 + count * 8)
    return pairs, pointer


def main(tool):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        media = [root / n for n in ['scene.bin', 'voice.adx', 'empty.dat']]
        values = [b'xyz', bytes(range(256)) * 17, b'']
        for name, value in zip(media, values):
            name.write_bytes(value)
        for named in (True, False):
            original = root / f'original-{named}.afs'
            run(tool, 'pack' if named else 'pack-unnamed', original, *media)
            data = original.read_bytes()
            pairs, (off, length) = entries(data)
            assert [size for _, size in pairs] == [len(value) for value in values]
            assert all(pos % 2048 == 0 for pos, _ in pairs)
            assert [data[s:s+n] for s, n in pairs] == values and len(data) % 2048 == 0
            if named:
                assert length == 144
                assert data[off:off + 48].split(b'\0', 1)[0] == b'scene.bin'
                assert b'\tvoice.adx\n' in run(tool, 'list', original)
                # Preserve opaque bytes and use a name that must never become an output path.
                updated = bytearray(data)
                updated[off:off + 32] = b'../unsafe'.ljust(32, b'\0')
                updated[off + 32:off + 48] = bytes(range(16))
                original.write_bytes(updated)
                data = bytes(updated)
            else:
                assert off == length == 0
            output = root / f'extracted-{named}'
            run(tool, 'extract', original, output)
            assert [f.read_bytes() for f in sorted(output.iterdir())] == values
            assert not (root / 'unsafe').exists()
            rebuilt = root / f'repacked-{named}.afs'
            run(tool, 'repack', original, rebuilt)
            assert rebuilt.read_bytes() == data
            run(tool, 'extract', original, output, ok=False)
            run(tool, 'repack', original, rebuilt, ok=False)
            assert rebuilt.read_bytes() == data
        # A missing ordinal has (0, 0), distinct from a stored empty file.
        holes = root / 'holes.afs'
        holes.write_bytes(struct.pack('<4s7I', b'AFS\0', 2, 0, 0, 24, 0, 0, 0))
        rebuilt = root / 'rebuilt-holes.afs'
        run(tool, 'repack', holes, rebuilt)
        pairs, pointer = entries(rebuilt.read_bytes())
        assert pairs[0] == (0, 0) and pairs[1] == (2048, 0) and pointer == (0, 0)
        print('AFS HOST TOOLS PASS')


if __name__ == '__main__':
    main(str(Path(sys.argv[1]).resolve()))
