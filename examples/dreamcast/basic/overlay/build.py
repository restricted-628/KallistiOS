#!/usr/bin/env python3
"""Build two original members and prove the resident slot survives final link."""
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
from dco_pack import pack, verify, SECTION
from elf_plan import ELF_HEADER


def run(command, quiet=False):
    command = list(map(str, command))
    print("$", " ".join(command), flush=True)
    result = subprocess.run(command, text=True, capture_output=True)
    print(("" if quiet else result.stdout) + result.stderr, end="", flush=True)
    result.check_returncode()
    return result.stdout


def symbols(nm, path):
    rows = run([nm, "-S", "-n", path], quiet=True)
    path.with_suffix(".nm").write_text(rows)
    result = {}
    for row in rows.splitlines():
        fields = row.split()
        if len(fields) == 4:
            result[fields[3]] = (int(fields[0], 16), int(fields[1], 16), fields[2])
        elif len(fields) == 3:
            result[fields[2]] = (int(fields[0], 16), 0, fields[1])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=HERE / ".build")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    tools = Path(os.environ["KOS_CC_BASE"]) / "bin"
    prefix = os.environ.get("KOS_CC_PREFIX", "sh-elf") + "-"
    nm, ld, objcopy = (tools / (prefix + name) for name in ("nm", "ld", "objcopy"))
    app, identity = out / "resident.o", out / "identity.o"
    metadata = out / "identity.c"
    metadata.write_text('#include <kos/overlay.h>\nconst ovl_expected_t member_identity[2] = {{0}};\n')
    common = ["kos-cc", "-std=gnu17", "-Wextra", "-Werror", "-I" + str(ROOT / "addons/include")]
    run(common + ["-c", HERE / "overlay.c", "-o", app])

    def link(name):
        run(common + ["-c", metadata, "-o", identity])
        elf = out / (name + ".elf")
        run(["kos-cc", "-Wl,-Map," + str(elf.with_suffix(".map")),
             "-o", elf, app, identity, ROOT / "addons/lib/dreamcast/libkosoverlay.a"])
        return elf, symbols(nm, elf)

    first, initial = link("reservation")
    base, size, _ = initial["_overlay_slot"]
    assert size == 8192 and base % 32 == 0
    manifests = []
    for number, letter in ((1, "A"), (2, "B")):
        obj, elf = out / (letter + ".o"), out / (letter + ".elf")
        run([tools / (prefix + "gcc"), "-std=gnu17", "-O2", "-ml", "-m4-single",
             "-Wa,--isa=sh4", "-Wall", "-Wextra", "-Werror", "-ffreestanding",
             "-fno-pic", "-fno-PIE", "-fno-common", "-fno-unwind-tables",
             "-fno-asynchronous-unwind-tables", "-DMEMBER_ID=" + str(number),
             "-c", HERE / "member.c", "-o", obj])
        run([ld, "-m", "shlelf", "--fatal-warnings", "--defsym=SLOT_BASE=" + str(base),
             "-T", ROOT / "utils/kos-overlay/fixture.ld", obj, "-o", elf])
        package, manifest = pack(elf.read_bytes(), base, size)
        assert verify(package, manifest) == manifest
        (out / (letter + ".DCO")).write_bytes(package)
        manifests.append(manifest)
    rows = []
    for manifest in manifests:
        digest = ",".join("0x" + manifest["package_sha256"][i:i+2] for i in range(0, 64, 2))
        rows.append("{%d,{%s}}" % (manifest["package_bytes"], digest))
    metadata.write_text('#include <kos/overlay.h>\nconst ovl_expected_t member_identity[2] = {\n' +
                        ",\n".join(rows) + "\n};\n")
    final, resolved = link("overlay")
    assert resolved["_overlay_slot"] == initial["_overlay_slot"], "slot moved after binding identity"
    heap = resolved["_end"][0]
    assert 0x8c010000 <= base < base + size <= heap < 0x8d000000 - 65536
    for name, (address, length, _kind) in resolved.items():
        if length and name != "_overlay_slot":
            assert address + length <= base or address >= base + size, ("overlapping symbol", name)
    raw = final.read_bytes()
    fields = ELF_HEADER.unpack_from(raw)
    sections = [SECTION.unpack_from(raw, fields[6] + i * fields[11]) for i in range(fields[12])]
    covering = [s for s in sections if s[2] & 2 and s[3] <= base and base + size <= s[3] + s[5]]
    assert len(covering) == 1 and covering[0][1] == 8, "slot must be resident NOBITS"
    assert all(s[3] + s[5] <= heap for s in sections if s[2] & 2 and 0x8c000000 <= s[3] < 0x8e000000)
    run([objcopy, "-O", "binary", final, out / "overlay.bin"])
    original = (out / "A.DCO").read_bytes()
    for name, offset in (("BADHEAD.DCO", 0), ("BADHASH.DCO", len(original) - 1)):
        damaged = bytearray(original)
        damaged[offset] ^= 1
        (out / name).write_bytes(damaged)
    report = {"execution_base": base, "slot_bytes": size, "heap_start": heap,
              "reservation": "NOBITS object, stable final VMA, no other symbol overlaps",
              "members": manifests, "elf_sha256": hashlib.sha256(raw).hexdigest(),
              "runtime_executed": False}
    (out / "build.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: stable reserved P1 slot, exact member identities and final-link bounds")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
