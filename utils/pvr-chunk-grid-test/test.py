#!/usr/bin/env python3
"""Imported grid success, CRC rejection, and truncated-asset cleanup checks.

Copyright (C) 2026 Joseph Black
"""

import pathlib
import struct
import subprocess
import sys
import tempfile

CASES = {"visible", "side-split", "side-drop", "depth-split", "depth-drop", "outside"}


def validate(text, target=False):
    assert "KOSGRID vertices=561 triangles=1024 strip_max=66" in text
    assert "KOSGRID result=PASS errno=0" in text
    assert "KOSGRID result=FAIL" not in text
    assert "KOSGRID parity=PASS guard=PASS short_sink=PASS" in text
    names = [line.split()[1].split("=", 1)[1] for line in text.splitlines()
             if line.startswith("KOSGRID case=") and line.endswith(" PASS")]
    assert len(names) == 6 and set(names) == CASES, names
    if target:
        rendered = []
        for line in text.splitlines():
            if line.startswith("KOSGRID render="):
                assert line.endswith(" PASS"), line
                fields = dict(word.split("=", 1) for word in line.split()[1:-1])
                assert fields["frames"] == "20" and fields["samples"] == "16", fields
                assert int(fields["mean_emit_us"]) >= 0, fields
                rendered.append(fields["render"])
        assert len(rendered) == 6 and set(rendered) == CASES, rendered


def run(executable, asset, success):
    result = subprocess.run([str(executable), str(asset)], text=True,
                            capture_output=True)
    print(result.stdout, end="")
    assert not result.stderr, result.stderr
    assert result.returncode == (0 if success else 1), result
    assert f"KOSGRID result={'PASS' if success else 'FAIL'}" in result.stdout, result
    if success:
        validate(result.stdout)


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "--log":
        for name in sys.argv[2:]:
            validate(pathlib.Path(name).read_text(errors="replace"), target=True)
            print(f"{name}: six grid cases, 120 frames, and cleanup PASS")
        raise SystemExit(0)
    executable = pathlib.Path(sys.argv[1]).resolve()
    asset = pathlib.Path(sys.argv[2]).resolve()
    run(executable, asset, True)
    original = asset.read_bytes()
    with tempfile.TemporaryDirectory(prefix="kos-grid-") as directory:
        bad = pathlib.Path(directory) / "bad.pcm"
        bad.write_bytes(original[:-1])
        run(executable, bad, False)
        changed = bytearray(original)
        sections = struct.unpack_from("<I", changed, 32)[0]
        for index in range(sections):
            kind, _, offset = struct.unpack_from("<3I", changed, 64 + index * 32)
            if kind == 1:  # Raw vertex stream: invalidate its decoded CRC.
                changed[offset + 8] ^= 1
                break
        else:
            raise AssertionError("missing vertex section")
        bad.write_bytes(changed)
        run(executable, bad, False)
    print("grid import, clipping, and failure cleanup tests passed")
