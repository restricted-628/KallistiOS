#!/usr/bin/env python3
"""Build original translated packages and the actual overlay/VM consumer probe."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / "utils/kos-overlay"))
from dco_pack import pack, verify, PROFILE_VM


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    out = parser.parse_args().out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    tool = Path(os.environ["KOS_CC_BASE"]) / "bin"
    prefix = os.environ.get("KOS_CC_PREFIX", "sh-elf") + "-"
    commands = []

    def run(cmd):
        cmd = list(map(str, cmd))
        commands.append(cmd)
        print("$", " ".join(cmd), flush=True)
        result = subprocess.run(cmd, text=True, capture_output=True)
        print(result.stdout + result.stderr, end="", flush=True)
        result.check_returncode()
        return result.stdout

    manifests = []
    for number, letter in ((1, "A"), (2, "B")):
        obj, elf = out / (letter + ".o"), out / (letter + ".elf")
        run([tool / (prefix + "gcc"), "-std=gnu17", "-O2", "-ml", "-m4-single",
             "-Wa,--isa=sh4", "-Wall", "-Wextra", "-Werror", "-ffreestanding",
             "-fno-pic", "-fno-PIE", "-fno-common", "-fno-unwind-tables",
             "-fno-asynchronous-unwind-tables", "-fno-optimize-sibling-calls",
             "-DMEMBER_ID=" + str(number), "-c", HERE / "member.c", "-o", obj])
        run([tool / (prefix + "ld"), "-m", "shlelf", "--fatal-warnings",
             "--defsym=SLOT_BASE=0x101ff000", "-T", HERE / "member.ld", obj, "-o", elf])
        symbols = run([tool / (prefix + "nm"), "-n", elf])
        (out / (letter + ".nm")).write_text(symbols)
        for n, name in enumerate(("_overlay_entry", "_page1", "_page2", "_page3", "_page4")):
            row = next(row for row in symbols.splitlines() if row.endswith(" " + name))
            assert int(row.split()[0], 16) == 0x101ff000 + n * 4096
        assert not run([tool / (prefix + "nm"), "-u", elf]).strip()
        (out / (letter + ".dis")).write_text(run([tool / (prefix + "objdump"), "-d", elf]))
        package, manifest = pack(elf.read_bytes(), 0x101ff000, 12 * 4096, PROFILE_VM)
        assert verify(package, manifest) == manifest
        assert len(package) > 32768
        (out / (letter + ".DCO")).write_bytes(package)
        manifests.append(manifest)
    rows = []
    for manifest in manifests:
        digest = ",".join("0x" + manifest["package_sha256"][i:i+2] for i in range(0, 64, 2))
        rows.append("{%d,{%s}}" % (manifest["package_bytes"], digest))
    identity = out / "identity.c"
    identity.write_text('#include <kos/overlay.h>\nconst ovl_expected_t member_identity[2] = {\n' +
                        ",\n".join(rows) + "\n};\n")
    objects = []
    for src in (HERE / "overlay-vm.c", identity):
        obj = out / (src.stem + ".o")
        run(["kos-cc", "-std=gnu17", "-Wextra", "-Werror", "-I" + str(ROOT / "addons/include"),
             "-c", src, "-o", obj])
        objects.append(obj)
    elf = out / "overlay-vm.elf"
    run(["kos-cc", "-Wl,-Map," + str(out / "overlay-vm.map"), "-o", elf,
         "-Wl,--wrap=vm_exec_begin,--wrap=vm_exec_abort,--wrap=vm_exec_retire,--wrap=vm_exec_write",
         *objects, ROOT / "addons/lib/dreamcast/libkosoverlay.a"])
    run([tool / (prefix + "objcopy"), "-O", "binary", elf, out / "overlay-vm.bin"])
    original = (out / "A.DCO").read_bytes()
    for name, offset in (("BADHEAD.DCO", 0), ("BADHASH.DCO", len(original) - 1)):
        damaged = bytearray(original)
        damaged[offset] ^= 1
        (out / name).write_bytes(damaged)
    record = {"members": manifests, "runtime_executed": False, "commands": commands,
              "compiler": run([tool / (prefix + "gcc"), "--version"]).splitlines()[0],
              "files": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in out.iterdir() if p.is_file()}}
    (out / "build.json").write_text(json.dumps(record, indent=2) + "\n")
    print("PASS: profile-2 packages, five instruction pages, crossing data/BSS, resident link")


if __name__ == "__main__":
    main()
