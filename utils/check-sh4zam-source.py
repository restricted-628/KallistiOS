#!/usr/bin/env python3
"""Verify the reviewed SH4ZAM pin and pristine upstream source."""

import argparse
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SUBMODULE = "addons/libsh4zam/upstream"
UPSTREAM_URL = "https://github.com/gyrovorbis/sh4zam.git"
REVIEWED_REVISION = "5b7dfc22a91f7a21756f1e8f86b1ed3d99dd33bf"


def git(root, *args):
    return subprocess.check_output(
        ["git", "-C", str(root), *args], stderr=subprocess.PIPE
    ).decode().strip()


def verify(root):
    """Reject source deltas and pins not yet reviewed by Integration."""
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
    if revision != REVIEWED_REVISION:
        raise ValueError("SH4ZAM source must be reviewed for this new pin")
    if git(upstream, "ls-files", "--others", "--exclude-standard"):
        raise ValueError("SH4ZAM upstream checkout has untracked files")
    if git(upstream, "diff", "--no-ext-diff", "--no-textconv",
           "--ignore-submodules=none", "HEAD", "--", "."):
        raise ValueError("SH4ZAM source must be pristine; reconcile local edits "
                         "using addons/libsh4zam/README.md")
    if git(upstream, "diff", "--cached", "--no-ext-diff", "--no-textconv",
           "--ignore-submodules=none", "HEAD", "--", "."):
        raise ValueError("SH4ZAM index must be pristine; preserve and reconcile staged edits")
    return revision


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    try:
        revision = verify(ROOT)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"SH4ZAM verification failed: {error}") from error
    print(f"SH4ZAM submodule: pristine reviewed upstream {revision}")


if __name__ == "__main__":
    main()
