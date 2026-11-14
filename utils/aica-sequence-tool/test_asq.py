import copy
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
import asq


def score():
    return {"programs": [{"tpqn": 96, "tempo_us": 500000, "events": [
        {"delta": 0, "kind": "midi", "status": 0xc0, "a": 1, "b": 0},
        {"delta": 0, "kind": "loop", "count": 2},
        {"delta": 0, "kind": "midi", "status": 0x90, "a": 60, "b": 100},
        {"delta": 96, "kind": "midi", "status": 0x80, "a": 60, "b": 0},
        {"delta": 0, "kind": "loop_end"},
        {"delta": 0, "kind": "tempo", "us": 400000},
        {"delta": 96, "kind": "end"}]}]}


def reseal(data):
    data = bytearray(data)
    struct.pack_into("<I", data, 32, zlib.crc32(data[64:]))
    return bytes(data)


def digest(blob):
    h = 2166136261
    def add(values):
        nonlocal h
        for value in values:
            for byte in struct.pack("<I", value):
                h = ((h ^ byte) * 16777619) & 0xffffffff
    count, total = struct.unpack_from("<II", blob, 16)
    for i in range(count):
        start, n, tpqn, tempo = struct.unpack_from("<IIII", blob, 64 + i * 32)
        add((n, tpqn, tempo))
        add(struct.unpack_from("<" + "I" * n * 4, blob, start))
    return f"1 {count} {total} {h:08x}"


class TestAsq(unittest.TestCase):
    def differential(self, blobs):
        data = b"".join(struct.pack("<I", len(b)) + b for b in blobs)
        run = subprocess.run([os.environ.get("ASQ_CHECK", "./native/asq-check")],
                             input=data, capture_output=True, check=True)
        lines = run.stdout.decode().splitlines()
        self.assertEqual(len(lines), len(blobs))
        for i, (blob, line) in enumerate(zip(blobs, lines)):
            try:
                manifest = asq.inspect(blob)
            except ValueError:
                self.assertEqual(line, "0", i)
            else:
                self.assertEqual(asq.build(manifest), blob)
                self.assertEqual(line, digest(blob), i)
        print(run.stderr.decode().strip(), file=sys.stderr)

    def test_independent_golden(self):
        manifest = {"programs": [{"tpqn": 1, "tempo_us": 1,
                     "events": [{"delta": 7, "kind": "end"}]}]}
        # Construct every field independently, including CRC bit recurrence.
        golden = bytearray(128)
        golden[:8] = b"KOSASQ\0\0"
        golden[8:12] = bytes((1, 0, 64, 0))
        for offset, value in [(12, 128), (16, 1), (20, 1), (24, 64), (28, 96),
                              (64, 96), (68, 1), (72, 1), (76, 1), (96, 7), (108, 5)]:
            golden[offset:offset + 4] = value.to_bytes(4, "little")
        crc = 0xffffffff
        for byte in golden[64:]:
            crc ^= byte
            for _ in range(8):
                crc = (crc >> 1) ^ (0xedb88320 if crc & 1 else 0)
        golden[32:36] = (crc ^ 0xffffffff).to_bytes(4, "little")
        self.assertEqual(asq.build(manifest), golden)
        self.differential([bytes(golden)] * 4)

    def test_roundtrip_truncations_mutations(self):
        blob = asq.build(score())
        cases = [blob] * 4 + [blob[:i] for i in range(len(blob))] + [blob + b"\0"]
        for i in range(len(blob)):
            for bit in (1, 128):
                changed = bytearray(blob)
                changed[i] ^= bit
                cases.append(bytes(changed))
                cases.append(reseal(changed))
        rng = random.Random(7341)
        for _ in range(1024):
            changed = bytearray(blob)
            for _ in range(rng.randrange(1, 8)):
                changed[rng.randrange(len(blob))] = rng.randrange(256)
            cases.append(reseal(changed))
        self.differential(cases)

    def test_loops_limits(self):
        cases = []
        for depth in range(5):
            m = score()
            events = ([{"delta": 0, "kind": "loop", "count": d} for d in range(depth)] +
                      [{"delta": 0xffffffff, "kind": "midi", "status": 0xe1, "a": 0, "b": 127}] +
                      [{"delta": 1, "kind": "loop_end"} for _ in range(depth)] +
                      [{"delta": 0xffffffff, "kind": "end"}])
            m["programs"][0].update(tpqn=9600, tempo_us=60000000, events=events)
            cases.append(asq.build(m))
        many = {"programs": [copy.deepcopy(score()["programs"][0]) for _ in range(128)]}
        cases.append(asq.build(many))
        maximum = score()
        maximum["programs"][0]["events"] = [{"delta": 0xffffffff, "kind": "tempo", "us": 1}] * 65535 + [{"delta": 0, "kind": "end"}]
        cases.append(asq.build(maximum))
        # Uneven boundary: one maximum-sized program is split into128 programs.
        maximum["programs"] = [{"tpqn": 1, "tempo_us": 1, "events":
            [{"delta": 0xffffffff, "kind": "tempo", "us": 1}] * 511 + [{"delta": 0, "kind": "end"}]} for _ in range(128)]
        cases.append(asq.build(maximum))
        self.differential(cases)

    def test_rejected_manifests(self):
        bad = []
        for field, value in [("tpqn", 0), ("tpqn", 9601), ("tempo_us", 0), ("tempo_us", 60000001), ("tpqn", True)]:
            m = score();m["programs"][0][field] = value;bad.append(m)
        for index, field, value in [(0, "status", 0xf0), (0, "a", 128), (0, "b", -1),
                                     (1, "count", 65536), (1, "delta", -1), (3, "delta", 0),
                                     (6, "delta", 0x100000000)]:
            m = score();m["programs"][0]["events"][index][field] = value;bad.append(m)
        m = score();m["programs"][0]["events"].pop();bad.append(m)
        m = score();m["programs"][0]["events"].append({"kind": "end", "delta": 0});bad.append(m)
        bad.extend([{"programs": []}, {"programs": [score()["programs"][0]] * 129}, {"programs": [], "unused": 1}])
        m = score();m["programs"][0]["events"] = [{"kind": "loop", "count": 1, "delta": 0}] * 5;bad.append(m)
        for m in bad:
            with self.assertRaises(ValueError):
                asq.build(m)

    def test_cli_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary);source = root / "score.json";output = root / "score.asq"
            source.write_text(json.dumps(score()))
            command = [sys.executable, "asq.py", "build", str(source), str(output)]
            subprocess.run(command, check=True)
            original = output.read_bytes()
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(output.read_bytes(), original)
            result = subprocess.run([sys.executable, "asq.py", "inspect", str(output)], capture_output=True, check=True)
            self.assertEqual(json.loads(result.stdout), score())
            output.unlink();source.write_text('{"programs": [], "programs": []}')
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertFalse(output.exists())
            self.assertEqual(list(root.glob(".asq-*")), [])


if __name__ == "__main__":
    unittest.main()
