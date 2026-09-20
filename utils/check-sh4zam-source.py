#!/usr/bin/env python3
"""Verify that the KOS adapter uses a clean, approved SH4ZAM source pin."""

import argparse
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SUBMODULE = "addons/libsh4zam/upstream"
UPSTREAM_URL = "https://github.com/gyrovorbis/sh4zam.git"
# Temporary exception for https://github.com/gyrovorbis/sh4zam/pull/70.
# Remove this exception when returning to an official upstream revision.
PR70_URL = "https://github.com/restricted-628/sh4zam.git"
PR70_REVISION = "0c1ccb5f5614314e36e2fec3179c8ca3ae844770"


def git(root, *args):
    return subprocess.check_output(
        ["git", "-C", str(root), *args], stderr=subprocess.PIPE
    ).decode().strip()


def verify(root):
    """Check source identity, cleanliness and the public header/license paths."""
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
    if url != UPSTREAM_URL and (url, revision) != (PR70_URL, PR70_REVISION):
        raise ValueError("SH4ZAM source is neither official upstream nor the exact approved PR #70 pin")
    if Path(git(upstream, "rev-parse", "--show-toplevel")).resolve() != upstream:
        raise ValueError("SH4ZAM path is not its own repository")
    if git(upstream, "rev-parse", "HEAD") != revision:
        raise ValueError("SH4ZAM checkout differs from the pinned Git revision")
    if git(upstream, "status", "--porcelain", "--untracked-files=all"):
        raise ValueError("SH4ZAM upstream checkout has local modifications")
    headers = root / "addons/include/sh4zam"
    if not headers.is_symlink() or headers.resolve() != upstream / "include/sh4zam":
        raise ValueError("Public SH4ZAM headers must link directly to the submodule")
    if (root / "addons/libsh4zam/LICENSE").read_bytes() != (upstream / "LICENSE").read_bytes():
        raise ValueError("SH4ZAM license copy differs from upstream")
    for stale in ("source", "source-lock.json", "local-changes.patch"):
        if (root / "addons/libsh4zam" / stale).exists():
            raise ValueError(f"Obsolete vendored SH4ZAM path remains: {stale}")
    return revision


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    try:
        revision = verify(ROOT)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"SH4ZAM verification failed: {error}") from error
    print(f"SH4ZAM submodule: clean approved source at {revision}")


if __name__ == "__main__":
    main()
