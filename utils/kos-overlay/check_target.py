#!/usr/bin/env python3
"""Compile/link original fixtures at P1 and translated VMAs; never execute."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from elf_plan import load_plan


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool-prefix", default="sh-elf-")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent

    def run(command):
        print("$", " ".join(map(str, command)), flush=True)
        result = subprocess.run(command, capture_output=True, text=True)
        print(result.stdout + result.stderr, end="", flush=True)
        if result.returncode:
            raise RuntimeError(f"command failed: {result.returncode}")
        return result.stdout

    run([args.tool_prefix + "gcc", "--version"])
    run([args.tool_prefix + "ld", "--version"])
    with tempfile.TemporaryDirectory(prefix="kos-overlay-check-") as tmp:
        tmp = Path(tmp)
        obj, elf = tmp / "member.o", tmp / "member.elf"
        run([args.tool_prefix + "gcc", "-ml", "-m4-single", "-std=gnu17", "-O2",
             "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-common", "-fno-pic",
             "-fno-asynchronous-unwind-tables", "-fno-unwind-tables", "-c",
             source / "fixture.c", "-o", obj])
        for base in (0x8c100000, 0x00100000):
            run([args.tool_prefix + "ld", "-m", "shlelf", "--fatal-warnings",
                 f"--defsym=SLOT_BASE={base}", "-T", source / "fixture.ld",
                 "-o", elf, obj])
            run([args.tool_prefix + "readelf", "-lW", elf])
            symbols = run([args.tool_prefix + "nm", "-n", elf])
            addresses = {p[2]: int(p[0], 16) for line in symbols.splitlines()
                         if len(p := line.split()) == 3}
            plan = load_plan(elf.read_bytes(), base, 8192)
            assert len(plan["segments"]) == 2
            assert base + plan["entry_offset"] == addresses["_overlay_entry"]
            data = plan["segments"][1]
            assert base + data["zero_offset"] == addresses["_overlay_state"]
            assert data["zero_size"] == 4
            assert data["memory_offset"] == 4096 and data["elf_permissions"] == 6
            command = [sys.executable, "-B", str(source / "elf_plan.py"), str(elf),
                       "--slot-base", str(base), "--slot-size", "8192"]
            first = subprocess.check_output(command)
            assert first == subprocess.check_output(command)
            assert json.loads(first) == plan
            bad = subprocess.run(command[:-1] + ["4096"], capture_output=True)
            assert bad.returncode == 1 and not bad.stdout
            assert b"outside execution slot" in bad.stderr
    print("PASS: two SH links, exact BSS/symbol checks, deterministic CLI and overflow rejection.")
    print("No target execution, mapping, DMA, cache or hardware validation.")


if __name__ == "__main__":
    main()
