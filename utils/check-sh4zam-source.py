#!/usr/bin/env python3
"""Verify the SH4ZAM pin and its exact, explicitly applied KOS patch."""

import argparse
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SUBMODULE = "addons/libsh4zam/upstream"
UPSTREAM_URL = "https://github.com/gyrovorbis/sh4zam.git"
PATCH_BASE = "c18faab269dd66fd9153536a1eda94e61b398aab"
PATCH = "addons/libsh4zam/patches/matrix-alignment.patch"


def git(root, *args):
    return subprocess.check_output(
        ["git", "-C", str(root), *args], stderr=subprocess.PIPE
    ).decode().strip()


def verify(root, apply_local_patch=False):
    """Reject every source delta except the reviewed patch on its exact base."""
    root = Path(root).resolve()
    upstream = root / SUBMODULE
    if not (upstream / ".git").exists():
        raise ValueError("SH4ZAM missing: run git submodule update --init --recursive")
    entry = git(root, "ls-files", "--stage", "--", SUBMODULE).split()
    if len(entry) != 4 or entry[0] != "160000" or entry[2] != "0":
        raise ValueError("SH4ZAM must be a resolved Git submodule in the index")
    revision = entry[1]
    url = git(root, "config", "--file", ".gitmodules", "--get",
              f"submodule.{SUBMODULE}.url")
    if url != UPSTREAM_URL:
        raise ValueError("SH4ZAM source must use the official repository")
    if Path(git(upstream, "rev-parse", "--show-toplevel")).resolve() != upstream:
        raise ValueError("SH4ZAM path is not its own repository")
    if git(upstream, "rev-parse", "HEAD") != revision:
        raise ValueError("SH4ZAM checkout differs from the pinned Git revision")
    headers = root / "addons/include/sh4zam"
    if not headers.is_symlink() or headers.resolve() != upstream / "include/sh4zam":
        raise ValueError("Public SH4ZAM headers must link directly to the submodule")
    if (root / "addons/libsh4zam/LICENSE").read_bytes() != (upstream / "LICENSE").read_bytes():
        raise ValueError("SH4ZAM license copy differs from upstream")
    for stale in ("source", "source-lock.json", "local-changes.patch"):
        if (root / "addons/libsh4zam" / stale).exists():
            raise ValueError(f"Obsolete vendored SH4ZAM path remains: {stale}")
    if revision != PATCH_BASE:
        raise ValueError("Local SH4ZAM patch must be reviewed for this new pin")
    if git(upstream, "ls-files", "--others", "--exclude-standard"):
        raise ValueError("SH4ZAM upstream checkout has untracked files")
    patch = (root / PATCH).read_bytes()

    def source_diff():
        return subprocess.check_output([
            "git", "-C", str(upstream), "-c", "core.quotePath=false", "diff",
            "--no-ext-diff", "--no-textconv", "--binary", "--full-index",
            "--no-renames", "--diff-algorithm=myers", "--unified=3",
            "--src-prefix=a/", "--dst-prefix=b/", "--no-color", "HEAD", "--", "."
        ])

    delta = source_diff()
    if not delta and apply_local_patch:
        subprocess.run(["git", "-C", str(upstream), "apply", "--check", "-"],
                       input=patch, check=True)
        subprocess.run(["git", "-C", str(upstream), "apply", "-"],
                       input=patch, check=True)
        delta = source_diff()
    if not delta:
        raise ValueError("KOS alignment patch missing: run "
                         "python3 utils/check-sh4zam-source.py --apply-local-patch")
    if delta != patch:
        raise ValueError("SH4ZAM source differs from the exact approved KOS patch")
    return revision


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply-local-patch", action="store_true",
                        help="apply the reviewed patch to a clean exact-base checkout")
    args = parser.parse_args()
    try:
        revision = verify(ROOT, args.apply_local_patch)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"SH4ZAM verification failed: {error}") from error
    print(f"SH4ZAM submodule: approved KOS alignment patch on {revision}")


if __name__ == "__main__":
    main()
