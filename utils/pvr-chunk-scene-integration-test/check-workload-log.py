#!/usr/bin/env python3
"""Check completeness of an emulator/console chunk-workload serial log.

This validates reported execution, not timing accuracy or hardware provenance.
Copyright (C) 2026 Joseph Black
"""

import pathlib
import sys


def check(path):
    content = path.read_text(errors="replace")
    expected = {(count, mode) for count in (1, 16, 256)
                for mode in ("shared", "independent")}
    cases = set()
    stages = set()
    final = 0
    for line in content.splitlines():
        if not line.startswith("KOSWORKLOAD "):
            continue
        fields = dict(word.split("=", 1) for word in line.split()[1:])
        if "result" in fields:
            assert fields == {"cases": "6", "frames": "192", "result": "PASS"}
            final += 1
            continue
        key = int(fields["pairs"]), fields["pose"]
        assert key in expected, fields
        if "stage" in fields:
            stage = fields["stage"]
            assert stage in {"pose", "draw", "ready", "cpu-frame"}, fields
            assert (key, stage) not in stages, fields
            stages.add((key, stage))
            assert int(fields["samples"]) == 24, fields
            assert 0 <= int(fields["min_us"]) <= int(fields["median_us"]) <= int(fields["max_us"]), fields
        else:
            assert key not in cases, fields
            cases.add(key)
            assert int(fields["triangles"]) == key[0] * 2, fields
            assert int(fields["vertices"]) == key[0] * 6, fields
            assert int(fields["warmup"]) == 8 and int(fields["frames"]) == 24, fields
            assert int(fields["drain_us"]) >= 0, fields
    assert cases == expected, cases
    assert len(stages) == 24 and final == 1, (stages, final)
    assert "KOSSCENE workload_checks=18 packet_guards=PASS lighting=PASS" in content
    assert "KOSSCENE result=PASS errno=0" in content
    assert "KOSSCENE result=FAIL" not in content
    print(f"{path.name}: six workload cases and final cleanup PASS")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("usage: check-workload-log.py SERIAL_LOG [SERIAL_LOG ...]")
    for argument in sys.argv[1:]:
        check(pathlib.Path(argument))
