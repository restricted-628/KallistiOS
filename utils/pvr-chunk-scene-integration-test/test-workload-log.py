#!/usr/bin/env python3
"""Positive and negative completeness checks for workload serial reports."""

import contextlib
import io
import pathlib
import runpy
import unittest

check = runpy.run_path(str(pathlib.Path(__file__).with_name("check-workload-log.py")))["check"]


class Log:
    name = "fixture.log"

    def __init__(self, text):
        self.text = text

    def read_text(self, **kwargs):
        return self.text


def complete_log():
    lines = ["KOSSCENE workload_checks=18 packet_guards=PASS lighting=PASS"]
    for pairs in (1, 16, 256):
        for mode in ("shared", "independent"):
            prefix = f"KOSWORKLOAD pairs={pairs} pose={mode}"
            lines.append(f"{prefix} triangles={pairs * 2} vertices={pairs * 6} warmup=8 frames=24 drain_us=0")
            for stage in ("pose", "draw", "ready", "cpu-frame"):
                lines.append(f"{prefix} stage={stage} samples=24 min_us=1 median_us=2 max_us=3")
    lines += ["KOSWORKLOAD cases=6 frames=192 result=PASS",
              "KOSSCENE result=PASS errno=0"]
    return "\n".join(lines)


class Reports(unittest.TestCase):
    def test_complete(self):
        with contextlib.redirect_stdout(io.StringIO()):
            check(Log(complete_log()))

    def test_rejections(self):
        good = complete_log()
        bad_logs = [
            good.rsplit("\n", 1)[0],
            good + "\n" + good.splitlines()[1],
            good + "\n" + good.splitlines()[2],
            good.replace("triangles=512", "triangles=511", 1),
            good.replace("samples=24", "samples=23", 1),
            good.replace("min_us=1", "min_us=4", 1),
            good.replace("stage=draw", "stage=unknown", 1),
            good.replace(good.splitlines()[2] + "\n", "", 1),
            good + "\nKOSSCENE result=FAIL errno=5",
        ]
        for log in bad_logs:
            with self.subTest(log=log[-100:]), self.assertRaises(AssertionError):
                check(Log(log))


if __name__ == "__main__":
    unittest.main()
