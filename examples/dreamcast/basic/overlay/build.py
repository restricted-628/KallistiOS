#!/usr/bin/env python3
"""Build the explicit P1 project and check its final resident reservation."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]


def run(command, quiet=False, env=None):
    command = list(map(str, command))
    print("$", " ".join(command), flush=True)
    result = subprocess.run(command, text=True, capture_output=True, env=env)
    print(("" if quiet else result.stdout) + result.stderr, end="", flush=True)
    result.check_returncode()
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=HERE / ".build")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    tools = Path(os.environ["KOS_CC_BASE"]) / "bin"
    prefix = os.environ.get("KOS_CC_PREFIX", "sh-elf") + "-"
    project = [sys.executable, "-B", ROOT / "utils/kos-overlay/project.py"]
    config = out / "overlays.json"
    config.write_bytes((HERE / "overlays.json").read_bytes())
    layout, packages = out / "layout", out / "packages"
    run(project + ["layout", config, layout])

    for number, name in ((1, "a"), (2, "b")):
        obj, elf = out / (name + ".o"), out / (name + ".elf")
        run([tools / (prefix + "gcc"), "-std=gnu17", "-O2", "-ml", "-m4-single",
             "-Wa,--isa=sh4", "-Wall", "-Wextra", "-Werror", "-ffreestanding",
             "-fno-pic", "-fno-PIE", "-fno-common", "-fno-unwind-tables",
             "-fno-asynchronous-unwind-tables", "-DMEMBER_ID=" + str(number),
             "-c", HERE / "member.c", "-o", obj])
        run([tools / (prefix + "ld"), "-m", "shlelf", "--fatal-warnings",
             "-T", layout / ("member_" + name + ".ld"),
             "-T", ROOT / "utils/kos-overlay/fixture.ld", obj, "-o", elf])
    run(project + ["package", config, packages])

    # Preserve provider selection; replace only the known default script.
    # The generated reservation goes in a private copy, not the shared script.
    default_script = ROOT / "utils/ldscripts/shlelf.xc"
    flags = os.environ["KOS_LDFLAGS"].split()
    script_flag = "-T" + str(default_script)
    if flags.count(script_flag) != 1 or any(
            flag.startswith("-T") and flag != script_flag for flag in flags):
        raise ValueError("example requires the checkout's default resident linker script")
    flags.remove(script_flag)
    script = default_script.read_text()
    boundary = "_end = .; PROVIDE (end = .);"
    if script.count(boundary) != 1:
        raise ValueError("expected one resident heap boundary")
    resident_script = out / "resident.ld"
    resident_script.write_text(script.replace(boundary,
                              (layout / "slots.ld").read_text() + boundary))
    env = dict(os.environ, KOS_LDFLAGS=" ".join(flags))
    common = ["kos-cc", "-std=gnu17", "-Wextra", "-Werror",
              "-I" + str(ROOT / "addons/include"), "-I" + str(packages)]
    app, catalog = out / "resident.o", out / "catalog.o"
    run(common + ["-c", HERE / "overlay.c", "-o", app])
    run(common + ["-c", packages / "dco_project.c", "-o", catalog])
    final = out / "overlay.elf"
    run(["kos-cc", "-T", resident_script, "-Wl,-Map," + str(out / "overlay.map"),
         "-o", final, app, catalog, ROOT / "addons/lib/dreamcast/libkosoverlay.a"], env=env)
    checked = json.loads(run(project + ["check-resident", config, final,
                                       "--ram-end", "0x8d000000", "--stack-bytes", "65536"]))
    (out / "overlay.nm").write_text(run([tools / (prefix + "nm"), "-S", "-n", final], quiet=True))
    run([tools / (prefix + "objcopy"), "-O", "binary", final, out / "overlay.bin"])
    # The fixture aliases retain exactly the generated catalog's package bytes.
    for name in ("a", "b"):
        (out / (name.upper() + ".DCO")).write_bytes((packages / ("member_" + name + ".dco")).read_bytes())
    original = (out / "A.DCO").read_bytes()
    for name, offset in (("BADHEAD.DCO", 0), ("BADHASH.DCO", len(original) - 1)):
        damaged = bytearray(original)
        damaged[offset] ^= 1
        (out / name).write_bytes(damaged)
    report = {"project": json.loads((packages / "project.json").read_text()),
              "resident_check": checked, "runtime_executed": False}
    (out / "build.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: generated P1 reservation, trusted catalog and final-link bounds")


if __name__ == "__main__":
    main()
