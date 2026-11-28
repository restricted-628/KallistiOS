"""Independent structural/wire checks and payload round trips for the host tool."""
from datetime import datetime, timezone
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def run(tool, *args, ok=True):
    result = subprocess.run([tool, *map(str, args)], capture_output=True,
                            env={**os.environ, 'TZ': 'UTC'})
    if (result.returncode == 0) != ok:
        raise AssertionError(result)
    return result.stdout


def entries(data):
    magic, count = struct.unpack_from('<4sI', data)
    assert magic == b'AFS\0'
    pairs = [struct.unpack_from('<II', data, 8 + n * 8) for n in range(count)]
    pointer = struct.unpack_from('<II', data, 8 + count * 8)
    return pairs, pointer


def check_file_lists(tool, root, media, values):
    stamp = datetime(2001, 2, 3, 4, 5, 57, tzinfo=timezone.utc).timestamp()
    for name in media:
        os.utime(name, (stamp, stamp))
    sub = root / 'payloads'
    sub.mkdir()
    (sub / 'MiX.Case.bin').write_bytes(b'new source')
    os.utime(sub / 'MiX.Case.bin', (stamp, stamp))

    for alignment in (2048, 4096, 6144):
        manifest = root / f'LIST{alignment}.ALS'
        manifest.write_bytes(b"voice.adx\r\nscene.bin\npayloads\\MiX.Case.bin")
        run(tool, 'pack-list', manifest, f'-dir={root}', f'-al={alignment}', '-hd=Pfx_')
        archive = manifest.with_suffix('.afs')
        data = archive.read_bytes()
        pairs, inline = entries(data)
        start = ((524288 + alignment - 1) // alignment) * alignment
        assert pairs[0] == (start, len(values[1])) and inline == (0, 0)
        assert all(offset % alignment == 0 for offset, _ in pairs)
        assert [data[off:off + count] for off, count in pairs] == [values[1], values[0], b'new source']
        off, count = struct.unpack_from('<II', data, 524288 - 8)
        assert count == 3 * 48 and len(data) == off + alignment
        assert data[off + 96:off + 128].rstrip(b'\0') == b'MiX.Case.bin'
        assert struct.unpack_from('<6HI', data, off + 128) == (2001, 2, 3, 4, 5, 56, 10)
        assert b'\tMiX.Case.bin\n' in run(tool, 'list', archive)
        ids = manifest.with_suffix('.h').read_text()
        assert '#define PFX_VOICE_ADX (0)' in ids and '#define PFX_MIX_CASE_BIN (2)' in ids
        assert manifest.with_suffix('.alg').read_text() == f'{archive} 0 {len(data)}\n'

    base = root / 'BASE.ALS'
    add = root / 'ADD.ALS'
    base.write_text('voice.adx\nscene.bin\n')
    run(tool, 'pack-list', base, f'-dir={root}')
    original_ids = base.with_suffix('.h').read_bytes()
    overlay_ids = root / 'overlay_ids.h'
    add.write_text('empty.dat\npayloads/MiX.Case.bin\nscene.bin\n')
    run(tool, 'pack-list', base, f'-dir={root}', f'-add={add}', f'--header={overlay_ids}')
    overlay = add.with_suffix('.afs')
    data = overlay.read_bytes()
    pairs, _ = entries(data)
    off, count = struct.unpack_from('<II', data, 524288 - 8)
    assert len(pairs) == 4 and pairs[0] == (0, 0)
    assert pairs[2][0] and pairs[2][1] == 0  # Present empty entry.
    assert pairs[1][1] == len(values[0]) and pairs[3][1] == 10
    assert count == 3 * 48
    assert [data[i:i + 32].rstrip(b'\0') for i in range(off, off + count, 48)] == [
        b'scene.bin', b'empty.dat', b'MiX.Case.bin']
    assert b'\tscene.bin\n' in run(tool, 'list', overlay)
    ids = overlay_ids.read_text()
    assert base.with_suffix('.h').read_bytes() == original_ids
    assert '#define VOICE_ADX (0)' in ids and '#define MIX_CASE_BIN (3)' in ids
    out = root / 'rebuilt-overlay.afs'
    run(tool, 'repack', overlay, out)
    assert run(tool, 'list', out).splitlines()[1].endswith(b'0\t0\t')

    # Sidecar collision must preserve all existing files and remove any newly
    # reserved path. The same rule applies if the archive path is occupied.
    collision = root / 'collision.als'
    collision.write_text('scene.bin\n')
    collision.with_suffix('.alg').write_bytes(b'existing log')
    run(tool, 'pack-list', collision, f'-dir={root}', ok=False)
    assert collision.with_suffix('.alg').read_bytes() == b'existing log'
    assert not collision.with_suffix('.h').exists() and not collision.with_suffix('.afs').exists()
    collision.with_suffix('.alg').unlink()
    run(tool, 'pack-list', collision, overlay, f'-dir={root}', ok=False)
    assert overlay.read_bytes() == data
    assert not collision.with_suffix('.alg').exists() and not collision.with_suffix('.h').exists()
    collision.write_bytes(b'scene.bin\nscene.bin\n')
    run(tool, 'pack-list', collision, f'-dir={root}', ok=False)
    assert not collision.with_suffix('.afs').exists()


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
        check_file_lists(tool, root, media, values)
        print('AFS HOST TOOLS PASS')


if __name__ == '__main__':
    main(str(Path(sys.argv[1]).resolve()))
