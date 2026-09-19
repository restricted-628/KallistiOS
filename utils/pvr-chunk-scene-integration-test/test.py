#!/usr/bin/env python3
"""Positive composition and failure-unwind checks for the shared example.

Copyright (C) 2026 Joseph Black
"""

import pathlib
import struct
import subprocess
import sys
import tempfile


def run(executable, source, success, stage=None):
    result = subprocess.run(
        [str(executable), str(source)], capture_output=True, text=True
    )
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, file=sys.stderr, end="")
    # Sanitizer failures may use the same exit status as an expected rejected
    # asset, so do not let an error-path memory diagnostic masquerade as PASS.
    assert not result.stderr, result
    expected = "KOSSCENE result=PASS" if success else "KOSSCENE result=FAIL"
    assert result.returncode == (0 if success else 1), result
    assert expected in result.stdout, result
    if stage:
        assert f"KOSSCENE stage={stage} " in result.stdout, result


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    source = pathlib.Path(sys.argv[2]).resolve()
    run(executable, source, True)
    original = source.read_bytes()
    with tempfile.TemporaryDirectory(prefix="kos-scene-test-") as temp:
        damaged = pathlib.Path(temp) / "damaged.pcm"
        damaged.write_bytes(original[:-1])
        run(executable, damaged, False, "scene-open")

        # Damage only the second skin payload. Model zero's cache has already
        # been materialized when its sibling fails, exercising partial cleanup.
        count = struct.unpack_from("<I", original, 32)[0]
        skins = []
        for index in range(count):
            kind, _, offset = struct.unpack_from("<3I", original, 64 + index * 32)
            if kind == 6:  # PCM2 general-skin section type
                skins.append(offset)
        assert len(skins) == 2
        corrupted = bytearray(original)
        corrupted[skins[1]] ^= 1
        damaged.write_bytes(corrupted)
        run(executable, damaged, False, "skin")
    print("compact scene integration and failure cleanup tests passed")


if __name__ == "__main__":
    main()
