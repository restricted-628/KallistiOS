#!/usr/bin/env python3
"""Verify or record the bundled SH4ZAM source and its upstream delta.

Copyright (C) 2026 Joseph Black
"""

import argparse
import difflib
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
BUNDLE = ROOT / "addons/libsh4zam"
LOCK = BUNDLE / "source-lock.json"
PATCH = BUNDLE / "local-changes.patch"


def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args])


def digest(data):
    return hashlib.sha256(data).hexdigest()


def mapping():
    paths = git(ROOT, "ls-files", "-z", "addons/include/sh4zam",
                "addons/libsh4zam/source", "addons/libsh4zam/LICENSE")
    result = {}
    for raw in paths.split(b"\0"):
        if not raw:
            continue
        local = raw.decode()
        if local.startswith("addons/include/"):
            upstream = local.removeprefix("addons/")
        else:
            upstream = local.removeprefix("addons/libsh4zam/")
        result[upstream] = local
    return dict(sorted(result.items()))


def check_upstream_inventory(path, revision, paths):
    upstream = git(path, "ls-tree", "-r", "--name-only", "-z", revision,
                   "--", "include/sh4zam", "source", "LICENSE")
    names = {name.decode() for name in upstream.split(b"\0") if name}
    if names != set(paths):
        raise SystemExit("Upstream file inventory changed; review additions "
                         "and removals before recording this revision")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path)
    parser.add_argument("--record", action="store_true")
    args = parser.parse_args()
    if args.record and not args.upstream:
        parser.error("--record requires --upstream")

    paths = mapping()
    if args.record:
        revision = git(args.upstream, "rev-parse", "HEAD").decode().strip()
        check_upstream_inventory(args.upstream, revision, paths)
        records = []
        patches = []
        for upstream, local in paths.items():
            original = git(args.upstream, "show", f"{revision}:{upstream}")
            bundled = (ROOT / local).read_bytes()
            records.append({"upstream_path": upstream, "local_path": local,
                            "upstream_sha256": digest(original),
                            "bundled_sha256": digest(bundled)})
            # Normalize line endings only in the human-readable patch. Hashes
            # above preserve the exact original bytes, including final EOLs.
            before = original.decode().splitlines(keepends=True)
            after = bundled.decode().splitlines(keepends=True)
            before = [line.rstrip("\r\n") + "\n" for line in before]
            after = [line.rstrip("\r\n") + "\n" for line in after]
            patches.extend(difflib.unified_diff(before, after,
                           fromfile="a/" + upstream, tofile="b/" + upstream))
        patch = "".join(patches).encode()
        lock = {"upstream_url": "https://github.com/gyrovorbis/sh4zam",
                "upstream_revision": revision,
                "patch_sha256": digest(patch), "files": records}
        PATCH.write_bytes(patch)
        LOCK.write_text(json.dumps(lock, indent=2) + "\n")

    lock = json.loads(LOCK.read_text())
    records = lock["files"]
    if args.upstream:
        check_upstream_inventory(args.upstream, lock["upstream_revision"], paths)
    expected = {item["upstream_path"]: item["local_path"] for item in records}
    if paths != expected:
        raise SystemExit("Vendored file inventory differs from source lock")
    if digest(PATCH.read_bytes()) != lock["patch_sha256"]:
        raise SystemExit("Local patch differs from source lock")
    for item in records:
        local = item["local_path"]
        if digest((ROOT / local).read_bytes()) != item["bundled_sha256"]:
            raise SystemExit(f"Bundled hash mismatch: {local}")
        if args.upstream:
            original = git(args.upstream, "show",
                           lock["upstream_revision"] + ":" + item["upstream_path"])
            if digest(original) != item["upstream_sha256"]:
                raise SystemExit(f"Upstream hash mismatch: {local}")
    print(f"SH4ZAM source lock: {len(records)} files verified; "
          f"upstream {lock['upstream_revision']}")


if __name__ == "__main__":
    main()
