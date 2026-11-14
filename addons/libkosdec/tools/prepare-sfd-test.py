#!/usr/bin/env python3
"""Prepare an actual supplied SFD and independent references. No downloads."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sfd", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--ffmpeg", default="ffmpeg")
    a = ap.parse_args()
    tool = shutil.which(a.ffmpeg)
    if not tool:
        raise SystemExit("SKIP: FFmpeg missing; references were not generated")
    data = a.sfd.read_bytes()
    if not data or len(data) > 4 * 1024 * 1024:
        raise SystemExit("input outside 4MiB profile")
    a.output.mkdir(parents=True, exist_ok=True)
    source = a.output / "movie.sfd"
    source.write_bytes(data)
    commands = []

    def run(options):
        cmd = [tool, "-hide_banner", "-loglevel", "error", "-y", "-i", str(source), *options]
        commands.append(cmd)
        subprocess.run(cmd, check=True)

    run(["-map", "0:v:0", "-c:v", "copy", "-f", "mpeg1video", str(a.output / "oracle.m1v")])
    run(["-map", "0:v:0", "-vsync", "0", "-pix_fmt", "yuv420p", "-f", "rawvideo",
         str(a.output / "reference.yuv")])
    if (a.output / "reference.yuv").stat().st_size != 360 * 115200:
        raise SystemExit("expected exactly 360 independent decoded frames")
    with (a.output / "movie_data.h").open("w") as f:
        f.write("/* Generated actual SFD bytes; never include in source patch. */\n")
        f.write("static const unsigned char movie_sfd[] = {\n")
        for i in range(0, len(data), 32):
            f.write(",".join(str(b) for b in data[i:i + 32]) + ",\n")
        f.write("};\n")
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    manifest = {"input": str(a.sfd.resolve()), "bytes": len(data),
                "ffmpeg": str(Path(tool).resolve()), "ffmpeg_sha256": sha(Path(tool)),
                "version": subprocess.check_output([tool, "-version"], text=True).splitlines()[0],
                "commands": commands,
                "sha256": {n: sha(a.output / n) for n in ("movie.sfd", "oracle.m1v", "reference.yuv")}}
    (a.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
