#!/usr/bin/env python3
"""Compile the actual standalone player selectors with retained profile cases."""
import argparse
from pathlib import Path
import shlex
import subprocess
p = argparse.ArgumentParser()
p.add_argument("build", type=Path)
p.add_argument("--cc", default="cc")
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
s = (root / "examples/video/main.c").read_text()
start = s.index("enum address_route {")
end = s.index("static bool oix_owned", start)
s = s[start:end].replace("static enum address_route oix_route;", "")
a.build.mkdir(parents=True, exist_ok=True)
c = a.build / "selector.c"
c.write_text("#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <assert.h>\n#include <stdio.h>\n" + s + (root / "tests/selector-body.c").read_text())
exe = (a.build / "selector").resolve()
subprocess.run(shlex.split(a.cc) + ["-std=c17", "-Wall", "-Wextra", "-Werror", str(c), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
