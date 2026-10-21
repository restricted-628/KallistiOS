#!/usr/bin/env python3
"""Check generated project inputs with original SH-4 links; no execution."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool-prefix", default="sh-elf-")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]

    def run(argv, cwd=None, fail=False):
        argv = list(map(str, argv))
        print("$", " ".join(argv), flush=True)
        result = subprocess.run(argv, cwd=cwd, text=True, capture_output=True)
        print(result.stdout + result.stderr, end="", flush=True)
        if fail:
            assert result.returncode != 0, "negative command unexpectedly succeeded"
        else:
            result.check_returncode()
        return result.stdout

    gcc, ld = args.tool_prefix + "gcc", args.tool_prefix + "ld"
    run([gcc, "--version"])
    run([ld, "--version"])
    with tempfile.TemporaryDirectory(prefix="kos-overlay-project-") as temp:
        temp = Path(temp)
        config = {"version": 1, "slots": [
            dict(name="game", profile="p1", base="0x8c100000", bytes=8192),
            dict(name="worker", profile="vm", base="0x100000", bytes=8192)],
            "members": [dict(name=n, slot=s, elf=n + ".elf")
                        for n, s in (("menu", "game"), ("play", "game"), ("logic", "worker"))]}
        manifest = temp / "overlays.json"
        manifest.write_text(json.dumps(config))
        tool = [sys.executable, "-B", here / "project.py"]
        run(tool + ["layout", manifest, temp / "layout"])
        flags = ["-ml", "-m4-single", "-Wa,--isa=sh4", "-std=gnu17", "-O2", "-Werror",
                 "-Wall", "-Wextra", "-ffreestanding", "-fno-common", "-fno-pic",
                 "-fno-unwind-tables", "-fno-asynchronous-unwind-tables"]
        run([gcc, *flags, "-c", here / "fixture.c", "-o", temp / "member.o"])
        for member in config["members"]:
            run([ld, "-m", "shlelf", "--fatal-warnings", "-T",
                 temp / "layout" / ("member_" + member["name"] + ".ld"),
                 "-T", here / "fixture.ld", temp / "member.o", "-o", temp / member["elf"]])
        run(tool + ["package", manifest, temp / "package"])
        run(tool + ["package", manifest, temp / "repeat"])
        for artifact in (temp / "package").iterdir():
            assert artifact.read_bytes() == (temp / "repeat" / artifact.name).read_bytes()
        run(tool + ["package", manifest, temp / "package"], fail=True)
        run([gcc, *flags, "-I" + str(root / "addons/include"), "-I" + str(root / "include"),
             "-I" + str(root / "kernel/arch/dreamcast/include"),
             "-c", temp / "package/dco_project.c", "-o", temp / "catalog.o"])
        (temp / "resident.c").write_text(
            '#include "dco_project.h"\nunsigned entry(void) {\n'
            ' return dco_project_members[DCO_MEMBER_MENU].expected.package_bytes +\n'
            ' dco_project_slots[DCO_SLOT_GAME].bytes;\n}\n')
        run([gcc, *flags, "-I" + str(temp / "package"), "-I" + str(root / "addons/include"),
             "-I" + str(root / "include"), "-I" + str(root / "kernel/arch/dreamcast/include"),
             "-c", temp / "resident.c", "-o", temp / "resident.o"])
        script = ('ENTRY(_entry)\nSECTIONS { . = 0x8c010000;\n'
                  '.text : { *(.text*) }\n.rodata : { *(.rodata*) }\n'
                  '.data : { *(.data*) }\n.bss (NOLOAD) : { *(.bss*) }\n'
                  'INCLUDE layout/slots.ld\n_end = .;\n}\n')
        (temp / "resident.ld").write_text(script)
        run([ld, "-m", "shlelf", "-T", "resident.ld", "resident.o", "catalog.o",
             "-o", "resident.elf"], cwd=temp)
        check = tool + ["check-resident", manifest, temp / "resident.elf",
                        "--ram-end", "0x8d000000", "--stack-bytes", "65536"]
        report = json.loads(run(check))
        assert report["checked_p1_slots"] == ["game"]
        assert report["heap_start"] == 0x8c102000
        assert not report["vm_backing_checked"]
        (temp / "bad.ld").write_text(script.replace('_end = .;', '_end = 0x8c010000;'))
        run([ld, "-m", "shlelf", "-T", "bad.ld", "resident.o", "catalog.o",
             "-o", "bad.elf"], cwd=temp)
        run([temp / "bad.elf" if str(a).endswith("resident.elf") else a for a in check], fail=True)
        run(check[:-1] + ["0xf00000"], fail=True)
    print("PASS: P1/VM members, generated catalog, deterministic packaging, final-link bounds/refusals.")
    print("SH-4 compile/link only; no emulator, installed-RAM, alias, cache or hardware proof.")


if __name__ == "__main__":
    main()
