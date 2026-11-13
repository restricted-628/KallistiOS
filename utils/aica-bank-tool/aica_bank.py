#!/usr/bin/env python3
"""Original, experimental AICA sample-bank authoring. No runtime upload.

The ABK draft format is private pending contract review. Python 3 standard
library only. All binary fields are explicitly little endian.
"""
import argparse
import json
from pathlib import Path
import struct
import sys
import wave
import zlib

MAGIC = b"KOSABK\0\0"
HEADER = struct.Struct("<8sHH9I16s")
SAMPLE = struct.Struct("<6IHHI")
ZONE = struct.Struct("<I8Bh11B7s")
MAX_FILE = 2 * 1024 * 1024
MAX_PAYLOAD = 0x1d0000
MAX_SAMPLES = 256
MAX_ZONES = 1024
MAX_MANIFEST = 256 * 1024
ZONE_FIELDS = (
    "bank", "program", "drum", "key_low", "key_high", "velocity_low",
    "velocity_high", "root_key", "fine_cents", "volume", "pan", "attack",
    "decay1", "decay2", "release", "decay_level", "key_rate", "direct",
    "effect_bus", "effect_send",
)
ZONE_DEFAULTS = (0, 0, 0, 0, 127, 1, 127, 60, 0, 255, 128,
                 31, 0, 0, 31, 0, 0, 15, 0, 0)


class BankError(ValueError):
    """An invalid or unsupported bank/input; nothing has been uploaded."""


def require(condition, message):
    if not condition:
        raise BankError(message)


def align32(size):
    return (size + 31) & ~31


def checksum(data):
    """CRC of the entire bank, with the CRC field treated as zero."""
    crc = zlib.crc32(data[:40])
    crc = zlib.crc32(b"\0" * 4, crc)
    return zlib.crc32(data[44:], crc) & 0xffffffff


def validate(data):
    """Validate before exposing any records. Returns metadata, never pointers."""
    require(HEADER.size <= len(data) <= MAX_FILE, "bank size out of range")
    (magic, version, hsize, total, ns, nz, so, zo, po, pb, crc,
     flags, reserved) = HEADER.unpack_from(data)
    require(magic == MAGIC and version == 1 and hsize == 64,
            "unsupported magic/version/header")
    require(total == len(data), "declared size differs from file")
    require(flags == 0 and not any(reserved), "unknown header flags/reserved")
    require(1 <= ns <= MAX_SAMPLES and 1 <= nz <= MAX_ZONES,
            "sample/zone count out of range")
    # Canonical tables disallow aliasing, gaps, overflow tricks and overlaps.
    require(so == 64 and zo == so + ns * SAMPLE.size and
            po == align32(zo + nz * ZONE.size), "noncanonical table offsets")
    require(po <= total and 0 < pb <= MAX_PAYLOAD and pb == total - po,
            "payload extent out of range")
    require(not any(data[zo + nz * ZONE.size:po]), "nonzero table padding")
    require(crc == checksum(data), "CRC mismatch")
    cursor = po
    samples = []
    for i in range(ns):
        off, size, frames, rate, ls, le, encoding, sf, sr = SAMPLE.unpack_from(
            data, so + i * SAMPLE.size)
        require(encoding in (1, 2), f"sample {i}: unsupported PCM encoding")
        require(sr == 0 and sf in (0, 1), f"sample {i}: unknown flags/reserved")
        require(1 <= frames <= 65534 and size == frames * encoding,
                f"sample {i}: frame/byte count mismatch")
        require(1 <= rate <= 192000, f"sample {i}: base rate out of range")
        require((sf == 0 and ls == le == 0) or
                (sf == 1 and 0 <= ls < le <= frames), f"sample {i}: invalid loop")
        require(off == cursor and size <= total - cursor,
                f"sample {i}: noncanonical/out-of-range data")
        end = off + size
        cursor = align32(end)
        require(cursor <= total and not any(data[end:cursor]),
                f"sample {i}: missing/nonzero padding")
        samples.append({"offset": off, "bytes": size, "frames": frames,
                        "rate": rate, "encoding": encoding,
                        "loop_start": ls, "loop_end": le})
    require(cursor == total, "trailing/unclaimed payload")
    zones = []
    groups = {}
    for i in range(nz):
        record = ZONE.unpack_from(data, zo + i * ZONE.size)
        sample, *values, reserved = record
        z = dict(zip(ZONE_FIELDS, values))
        require(sample < ns and not any(reserved), f"zone {i}: reference/reserved")
        require(z['bank'] <= 127 and z['program'] <= 127 and z['drum'] <= 1,
                f"zone {i}: program identity")
        require(0 <= z['key_low'] <= z['key_high'] <= 127 and
                1 <= z['velocity_low'] <= z['velocity_high'] <= 127 and
                z['root_key'] <= 127 and -100 <= z['fine_cents'] <= 100,
                f"zone {i}: key/velocity/tuning")
        require(all(z[k] <= 31 for k in
                    ('attack', 'decay1', 'decay2', 'release', 'decay_level')) and
                all(z[k] <= 15 for k in ('key_rate', 'direct', 'effect_bus', 'effect_send')),
                f"zone {i}: envelope/routing")
        key = (z['bank'], z['program'], z['drum'])
        for old in groups.get(key, ()):
            require(z['key_high'] < old['key_low'] or old['key_high'] < z['key_low'] or
                    z['velocity_high'] < old['velocity_low'] or
                    old['velocity_high'] < z['velocity_low'],
                    f"zone {i}: ambiguous overlapping key/velocity ranges")
        groups.setdefault(key, []).append(z)
        zones.append(dict(sample=sample, **z))
    return {"version": version, "size": total, "payload_bytes": pb,
            "samples": samples, "zones": zones}


def read_bounded(path, limit):
    with Path(path).open('rb') as stream:
        data = stream.read(limit + 1)
    require(len(data) <= limit, f"input exceeds {limit} bytes: {path}")
    return data


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def keys(obj, allowed, required):
    require(isinstance(obj, dict) and set(required) <= obj.keys() and
            obj.keys() <= set(allowed), "missing or unknown manifest fields")


def integer(value, lo, hi, name):
    require(type(value) is int and lo <= value <= hi, f"invalid {name}")
    return value


def build(manifest, directory):
    """Build from mono PCM WAVs; output is validated before it is returned."""
    keys(manifest, ('version', 'samples', 'zones'), ('version', 'samples', 'zones'))
    require(type(manifest['version']) is int and manifest['version'] == 1,
            "unsupported manifest version")
    ss, zz = manifest['samples'], manifest['zones']
    require(isinstance(ss, list) and 1 <= len(ss) <= MAX_SAMPLES and
            isinstance(zz, list) and 1 <= len(zz) <= MAX_ZONES, "manifest counts")
    names, records, payload = {}, [], bytearray()
    po = align32(64 + len(ss) * 32 + len(zz) * 32)
    for i, s in enumerate(ss):
        keys(s, ('name', 'wav', 'loop_start', 'loop_end'), ('name', 'wav'))
        name = s['name']
        require(isinstance(name, str) and 1 <= len(name) <= 64 and name not in names,
                "invalid/duplicate sample name")
        require(isinstance(s['wav'], str) and 0 < len(s['wav']) <= 4096,
                "invalid WAV path")
        path = Path(directory) / s['wav']
        # Bound the entire WAV, including unknown RIFF chunks, before parsing.
        import io
        with wave.open(io.BytesIO(read_bounded(path, MAX_FILE)), 'rb') as wav:
            frames, width, rate = wav.getnframes(), wav.getsampwidth(), wav.getframerate()
            require(wav.getnchannels() == 1 and wav.getcomptype() == 'NONE' and
                    width in (1, 2) and 1 <= frames <= 65534 and 1 <= rate <= 192000,
                    "WAV must be bounded mono PCM8/PCM16")
            pcm = wav.readframes(frames)
            require(len(pcm) == frames * width, "truncated WAV sample data")
        if width == 1:
            pcm = bytes(v ^ 0x80 for v in pcm)  # WAV unsigned -> AICA signed.
        loop = 'loop_start' in s or 'loop_end' in s
        require(not loop or ('loop_start' in s and 'loop_end' in s), "incomplete loop")
        ls = integer(s.get('loop_start', 0), 0, frames, 'loop_start')
        le = integer(s.get('loop_end', 0), 0, frames, 'loop_end')
        require(not loop or ls < le, "empty/reversed loop")
        require(len(payload) + align32(len(pcm)) <= MAX_PAYLOAD, "payload budget exceeded")
        records.append(SAMPLE.pack(po + len(payload), len(pcm), frames, rate,
                                   ls, le, width, int(loop), 0))
        payload.extend(pcm)
        payload.extend(bytes(align32(len(payload)) - len(payload)))
        names[name] = i
    zones = []
    for z in zz:
        keys(z, ('sample',) + ZONE_FIELDS, ('sample',))
        require(isinstance(z['sample'], str) and z['sample'] in names,
                "unknown instrument sample")
        values = [integer(z.get(k, default), -100 if k == 'fine_cents' else 0,
                          100 if k == 'fine_cents' else 255, k)
                  for k, default in zip(ZONE_FIELDS, ZONE_DEFAULTS)]
        zones.append(ZONE.pack(names[z['sample']], *values, bytes(7)))
    data = bytearray(HEADER.pack(MAGIC, 1, 64, po + len(payload), len(ss), len(zz),
                                64, 64 + len(ss) * 32, po, len(payload), 0, 0, bytes(16)))
    data.extend(b''.join(records))
    data.extend(b''.join(zones))
    data.extend(bytes(po - len(data)))
    data.extend(payload)
    struct.pack_into('<I', data, 40, checksum(data))
    validate(data)
    return bytes(data)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    check = commands.add_parser('check', help='validate an original ABK draft bank')
    check.add_argument('bank', type=Path)
    pack = commands.add_parser('build', help='build a bank from a JSON manifest and WAVs')
    pack.add_argument('manifest', type=Path)
    pack.add_argument('output', type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == 'check':
            result = validate(read_bounded(args.bank, MAX_FILE))
        else:
            manifest = json.loads(read_bounded(args.manifest, MAX_MANIFEST),
                                  object_pairs_hook=unique_object)
            data = build(manifest, args.manifest.parent)
            result = validate(data)
            # Refuse replacement; a failed validation never creates an output.
            with args.output.open('xb') as output:
                output.write(data)
        print(json.dumps(result, indent=2))
        return 0
    except (BankError, OSError, ValueError, EOFError, wave.Error, RecursionError) as error:
        print(f'aica-bank: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
