#!/usr/bin/env python3
"""Verify the unmodified SH4DER-PVR dependency and direct header links."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SUBMODULE = "addons/libsh4der/upstream"
PIN = "17a1f0623505f715824ab8cee85d45f16b9c3ebc"
URL = "https://github.com/JiltedValkyrie/SH4DER-PVR.git"


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()


def verify():
    vendor = ROOT / SUBMODULE
    if not (vendor / ".git").exists():
        raise ValueError("initialize the pinned submodule first")
    if git(ROOT, "ls-files", "--stage", SUBMODULE).split() != ["160000", PIN, "0", SUBMODULE]:
        raise ValueError("SH4DER-PVR index pin differs from the reviewed revision")
    if git(ROOT, "config", "--file", ".gitmodules", "--get", f"submodule.{SUBMODULE}.url") != URL:
        raise ValueError("unexpected SH4DER-PVR repository")
    if Path(git(vendor, "rev-parse", "--show-toplevel")).resolve() != vendor.resolve():
        raise ValueError("vendor path is not its own repository")
    if git(vendor, "rev-parse", "HEAD") != PIN or git(vendor, "status", "--porcelain", "--untracked-files=all"):
        raise ValueError("vendor checkout is not the clean reviewed pin")
    for name, target in (("sh4der", "include"),
                         ("sh4dow_dc.h", "sh4dow-dc/include/sh4dow_dc.h")):
        link = ROOT / "addons/include" / name
        if not link.is_symlink() or link.resolve() != vendor / target:
            raise ValueError(f"unexpected public header link: {name}")
    for license_path in ("LICENSE", "sh4dow-dc/LICENSE"):
        if not (vendor / license_path).is_file():
            raise ValueError(f"missing dependency license: {license_path}")


if __name__ == "__main__":
    try:
        verify()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"SH4DER-PVR verification failed: {error}") from error
    print(f"SH4DER-PVR: clean pinned dependency {PIN}")
