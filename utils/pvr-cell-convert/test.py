#!/usr/bin/env python3
"""Golden and rejection tests for pvr-cell-convert.

Copyright (C) 2026 Joseph Black
"""

import os
import pathlib
import shlex
import struct
import subprocess
import sys
import tempfile
import zlib


def invoke(*arguments):
    return subprocess.run(
        [*map(str, arguments)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def png(path, width, height):
    def chunk(kind, payload):
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(
            ">I", zlib.crc32(body) & 0xFFFFFFFF
        )

    rows = b"".join(b"\0" + b"\xff\xff\xff\xff" * width
                    for _ in range(height))
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows))
        + chunk(b"IEND", b"")
    )


def compile_atlas(source, root):
    repository = pathlib.Path(__file__).resolve().parents[2]
    object_path = root / "atlas.o"
    command = [
        *shlex.split(os.environ.get("CC", "cc")),
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-I", str(repository / "utils/pvr-geometry-test/include"),
        "-I", str(repository / "kernel/arch/dreamcast/include"),
        "-c", str(source),
        "-o", str(object_path),
    ]
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    assert result.returncode == 0, result.stderr


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test.py CONVERTER")
    converter = sys.argv[1]

    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        image = root / "atlas.png"
        manifest = root / "hero.pcell"
        asset = root / "hero.pca"
        atlas = root / "hero_atlas.c"
        png(image, 64, 32)
        manifest.write_text(
            """pvr-cell 1
region 0 0 16 16 8 8
region 16 0 32 16 4 12
cell 0 1 2 3 0 1 1 -2 0 4 0xffffffff 0xffffffff 0xffffffff 0xffffffff 0 0 0 0
stream 0.125 1 repeat
key-atlas 0.25 0 1
key-offset 0.25 0 8 9 10
key-diffuse 0.5 0 0xff102030 0xff405060 0xff708090 0xffa0b0c0
end-stream
stream -0.25 2 clamp
key-rotation 0.25 0 1.5
key-scale 0.5 0 2 3
key-priority 0.75 0 6
key-flags 1 0 3
key-material 1.25 0 7
key-specular 1.5 0 0xff000001 0xff000002 0xff000003 0xff000004
end-stream
""",
            encoding="ascii",
        )
        result = invoke(
            converter, "--image", image, "--symbol", "hero_atlas",
            manifest, asset, atlas,
        )
        assert result.returncode == 0, result.stderr
        assert result.stdout == (
            "regions=2\ncells=1\nstreams=2\nasset_bytes=968\n"
        )
        data = asset.read_bytes()
        assert data[:4] == b"PCA1"
        assert struct.unpack_from("<HHIIII", data, 4) == (1, 64, 968, 1, 2, 9)
        assert zlib.crc32(data[64:]) & 0xFFFFFFFF == struct.unpack_from(
            "<I", data, 44
        )[0]
        assert zlib.crc32(data[:60]) & 0xFFFFFFFF == struct.unpack_from(
            "<I", data, 60
        )[0]
        generated = atlas.read_text(encoding="ascii")
        assert "const pvr_sprite_atlas_t hero_atlas" in generated
        assert "0x1p-1F" in generated
        assert "0x1p-2F" in generated
        compile_atlas(atlas, root)

        bad = root / "bad.pcell"
        bad.write_text(
            """pvr-cell 1
region 60 0 8 8 0 0
cell 0 0 0 0 0 1 1 0 0 0 0xffffffff 0xffffffff 0xffffffff 0xffffffff 0 0 0 0
""",
            encoding="ascii",
        )
        bad_asset = root / "bad.pca"
        bad_atlas = root / "bad.c"
        result = invoke(
            converter, "--image", image, "--symbol", "bad_atlas",
            bad, bad_asset, bad_atlas,
        )
        assert result.returncode != 0
        assert not bad_asset.exists()
        assert not bad_atlas.exists()

        unordered = root / "unordered.pcell"
        unordered.write_text(
            """pvr-cell 1
region 0 0 8 8 0 0
cell 0 0 0 0 0 1 1 0 0 0 0xffffffff 0xffffffff 0xffffffff 0xffffffff 0 0 0 0
stream 0 1 clamp
key-priority 0.75 0 2
key-priority 0.25 0 1
end-stream
""",
            encoding="ascii",
        )
        result = invoke(
            converter, "--image", image, "--symbol", "unordered_atlas",
            unordered, root / "unordered.pca", root / "unordered.c",
        )
        assert result.returncode != 0

        result = invoke(
            converter, "--image", image, "--symbol", "for",
            manifest, root / "keyword.pca", root / "keyword.c",
        )
        assert result.returncode != 0

    print("pvr-cell-convert tests passed")


if __name__ == "__main__":
    main()
