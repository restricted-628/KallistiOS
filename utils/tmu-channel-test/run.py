"""Compile the production TMU driver with host registers/IRQ/allocation shims."""
# Copyright (C) 2026 Joseph Black
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", default="cc")
parser.add_argument("--cflags", default="-O2 -std=gnu17 -Wall -Wextra -Werror")
parser.add_argument("--source", type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parent.parent
source = (args.source or root / "kernel/arch/dreamcast/kernel/timer.c").read_text()
for width in (8, 16, 32):
    source, count = re.subn(rf"^#define TIMER{width}\(o\).*?$",
                           f"#define TIMER{width}(o) (test_regs{width}[(o)])",
                           source, flags=re.M)
    assert count == 1, (width, count)
with tempfile.TemporaryDirectory(prefix="kos-tmu-test.") as directory:
    unit = Path(directory) / "driver.c"
    unit.write_text('#include "shim.h"\n' + source + '\n#include "test.c"\n')
    executable = Path(directory) / "test"
    subprocess.run(shlex.split(args.cc) + shlex.split(args.cflags) +
                   ["-I" + str(here), "-I" + str(here / "include"),
                    "-I" + str(root / "kernel/arch/dreamcast/include"),
                    str(unit), "-o", str(executable)],
                   check=True)
    subprocess.run([str(executable)], check=True)
