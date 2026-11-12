"""Original CLI/EOF regression fixtures; no SDK or game assets required."""
from pathlib import Path
import errno
import hashlib
import itertools
import json
import os
import struct
import subprocess
import sys
import tempfile

tool = str(Path(sys.argv[1]).resolve())
checks = 0


def asset(rate=44100, channels=2, version=3, frames=65, header=50, footer=18, loop=False):
    data = bytearray(header)
    struct.pack_into('>HHBBBBIIHBB', data, 0, 0x8000, header - 4,
                     3, 18, 4, channels, rate, frames, 500, version, 0)
    if loop:
        struct.pack_into('>HHIIIII', data, 20, 0, 1, 1, 32,
                         header + 18 * channels, 65, header + 54 * channels)
    data[-6:] = b'(c)CRI'
    for group in range((frames + 31) // 32):
        for channel in range(channels):
            data.extend(bytes([0, channel + 1]) + bytes((i * 7 + group + channel) % 256 for i in range(16)))
    data.extend(struct.pack('>HH', 0x8001, footer - 4) + bytes(footer - 4))
    return data


def invoke(paths, code):
    global checks
    proc = subprocess.run([tool, '--', *map(str, paths)], capture_output=True, timeout=30)
    assert proc.returncode == code, (proc.returncode, proc.stderr)
    rows = [json.loads(line) for line in proc.stdout.splitlines()]
    assert len(rows) == len(paths)
    for path, row in zip(paths, rows):
        assert row['path_bytes'].encode('latin-1') == os.fsencode(path)
        assert row['compatible'] == (row['status'] == 'compatible')
    checks += len(rows)
    return rows


with tempfile.TemporaryDirectory(prefix='adxcheck-') as tmp:
    root = Path(tmp)
    good = []
    hashes = {}
    for rate, channel, version, frames, header, footer in itertools.product(
            [8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000], [1, 2], [3, 5], [0, 1, 65], [26, 50, 4099], [18, 513]):
        path = root / f'{len(good)}.adx'
        payload = asset(rate, channel, version, frames, header, footer)
        path.write_bytes(payload)
        hashes[path] = hashlib.sha256(payload).hexdigest()
        good.append(path)
    rows = invoke(good, 0)
    for path, row in zip(good, rows):
        assert row['decoded_frames'] == row['declared']['frames']
        assert row['validated_header']['frames'] == row['declared']['frames']
        assert row['bytes_consumed'] == path.stat().st_size
        assert row['validated_header']['loop'] is None
        assert row['io_error'] is None
        assert hashlib.sha256(path.read_bytes()).hexdigest() == hashes[path]

    for header, footer, frames in [(65539, 65539, 65), (26, 4052, 32), (50, 18, 65)]:
        path = root / f'boundary-{header}.adx'
        payload = asset(channels=2 if header != 26 else 1, frames=frames,
                        header=header, footer=footer, loop=header == 50)
        path.write_bytes(payload)
        row = invoke([path], 0)[0]
        assert row['bytes_consumed'] == len(payload)
        if header == 26:
            assert len(payload) == 4096  # Requires a separate read to discover EOF.
        if header == 50:
            assert row['validated_header']['loop'] == dict(
                start_frame=32, end_frame=65, start_byte=86, end_byte=158)

    for rate, cutoff in [(1,0),(2,1),(7999,3999),(8000,0),(8000,4000),
                         (12345,123),(44100,499),(44100,1000),(48000,1),(48000,24000)]:
        path = root / f'general-{rate}-{cutoff}.adx'
        payload = asset(rate=rate)
        struct.pack_into('>H', payload, 16, cutoff)
        path.write_bytes(payload)
        row = invoke([path], 0)[0]
        assert row['default_player_rate_supported'] == (8000 <= rate <= 48000)
        assert row['validated_header']['sample_rate'] == rate
    base = asset()
    truncated = []
    for end in range(len(base)):
        path = root / f'truncated-{end}.adx'
        path.write_bytes(base[:end])
        truncated.append(path)
    assert all(r['status'] == 'truncated' for r in invoke(truncated, 1))

    bad = []
    for name, index, value, expected in [
            ('signature', 0, 0, 'invalid'), ('encoding', 4, 4, 'unsupported'),
            ('block', 5, 17, 'unsupported'), ('bits', 6, 8, 'unsupported'),
            ('channels', 7, 3, 'unsupported'), ('rate', 8, 1, 'unsupported'),
            ('cutoff', 16, 255, 'unsupported'), ('version', 18, 4, 'unsupported'),
            ('flags', 19, 1, 'unsupported'), ('marker', 49, 0, 'invalid'),
            ('scale', 50, 0x80, 'invalid'), ('footer', len(base)-18, 0, 'invalid'),
            ('footer_payload', len(base)-1, 1, 'invalid')]:
        payload = base.copy(); payload[index] = value
        path = root / (name + '.adx'); path.write_bytes(payload)
        bad.append((path, expected))
    for path, row in zip(bad, invoke([x[0] for x in bad], 1)):
        assert row['status'] == path[1], (path, row)
        if path[1] == 'unsupported':
            assert row['validated_header'] is None
            assert row['default_player_rate_supported'] is None
            assert row['declared'] is not None
    for name, payload in [('trailing', base + b'\0'), ('concatenated', base * 2)]:
        path = root / (name + '.adx'); path.write_bytes(payload)
        assert invoke([path], 1)[0]['status'] == 'invalid'

    names = ['quote"\\\n.adx', 'music-\N{MUSICAL NOTE}.adx', os.fsdecode(b'invalid-utf8-\xff.adx'), '--help']
    for name in names:
        path = root / name
        try:
            path.write_bytes(base)
        except OSError as error:
            # Some filesystems reject non-UTF-8 names. Still verify byte-preserving
            # JSON for the actual failed open rather than omitting that case.
            if error.errno != errno.EILSEQ:
                raise
            assert invoke([path], 1)[0]['status'] == 'io_error'
        else:
            invoke([path], 0)
    missing = root / 'missing.adx'
    for row in invoke([missing, root], 1):
        assert row['status'] == 'io_error' and row['io_error']
    mixed = invoke([missing, good[0], bad[0][0], good[-1]], 1)
    assert [x['compatible'] for x in mixed] == [False, True, False, True]
    for args, expected in [([], 2), (['--'], 2), (['--help'], 0)]:
        assert subprocess.run([tool, *args], capture_output=True, timeout=10).returncode == expected
        checks += 1
    descriptor = os.open(os.devnull, os.O_RDONLY)
    try:
        assert subprocess.run([tool, str(good[0])], stdout=descriptor,
                              stderr=subprocess.PIPE, timeout=10).returncode == 2
        checks += 1
    finally:
        os.close(descriptor)
print(f'adxcheck: {checks} CLI/file cases passed')
