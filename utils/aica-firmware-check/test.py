#!/usr/bin/env python3
"""Regression: equal relocation indexes may name different call targets."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from check import Elf, require


def unresolved(elf):
    return {name: (s[1], s[2], s[5], b"" if s[1] == 8 else elf.section_bytes(s))
            for name, s in elf.named.items() if s[2] & 2 or s[1] in (4, 9)}


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="aica-relocation-test-") as temporary:
    root = Path(temporary)
    objects = []
    for target in ("foo", "bar"):
        source, output = root / (target + ".c"), root / (target + ".o")
        source.write_text("extern int " + target + "(void);\n"
                          "int call(void) { return " + target + "(); }\n")
        subprocess.run([args.cc, "-mcpu=arm7di", "-O2", "-c", str(source), "-o", str(output)], check=True)
        objects.append(Elf(output))
    require(unresolved(objects[0]) == unresolved(objects[1]), "fixture must have equal code and raw relocations")
    require(objects[0].allocated() != objects[1].allocated(), "different resolved call targets must be rejected")
print("AICA relocation identity: equal raw indexes, different call targets rejected")
