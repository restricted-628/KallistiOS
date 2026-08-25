#!/usr/bin/env python3
"""Golden and malformed-input tests for pvr-model-inspect.

Copyright (C) 2026 Joseph Black
"""

import pathlib
import struct
import subprocess
import sys
import tempfile


EXPECTED = """valid=1
vertex_words=12
polygon_words=18
vertex_records=1
vertex_entries=3
shape_records=0
polygon_records=5
material_records=1
strip_records=1
strips=1
triangles=1
index_references=3
maximum_vertex_index=2
texture_references=3
distinct_textures=2
maximum_strip_vertices=3
"""


def invoke(tool, *arguments):
    return subprocess.run(
        [tool, *map(str, arguments)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def main():
    tool = sys.argv[1] if len(sys.argv) == 2 else "./pvr-model-inspect"
    vertices = [
        34 | (10 << 16),
        3 << 16,
        0xBF800000, 0xBF800000, 0,
        0x3F800000, 0xBF800000, 0,
        0, 0x3F800000, 0,
        0xFF,
    ]
    polygons = [
        8, 5,
        8, 5,
        8, 9,
        17, 2, 0xFFFF, 0xFFFF,
        64, 5, 1, 3, 0, 1, 2,
        0xFF,
    ]

    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        vertex_path = root / "vertices.bin"
        polygon_path = root / "polygons.bin"
        vertex_path.write_bytes(struct.pack(f"<{len(vertices)}I", *vertices))
        polygon_path.write_bytes(struct.pack(f"<{len(polygons)}H", *polygons))

        result = invoke(tool, "--center", "0", "0", "0", "--radius",
                        "1.5", vertex_path, polygon_path)
        assert result.returncode == 0, result.stderr
        assert result.stdout == EXPECTED, result.stdout
        assert not result.stderr, result.stderr

        result = invoke(tool, "--", vertex_path, polygon_path)
        assert result.returncode == 0, result.stderr
        assert result.stdout == EXPECTED

        truncated = root / "truncated.bin"
        truncated.write_bytes(polygon_path.read_bytes()[:-2])
        result = invoke(tool, vertex_path, truncated)
        assert result.returncode == 1, (result.returncode, result.stderr)
        assert result.stdout == ""
        assert result.stderr.startswith("invalid model: ")

        wrong_width = root / "wrong-width.bin"
        wrong_width.write_bytes(b"\x00\x01\x02")
        result = invoke(tool, wrong_width, polygon_path)
        assert result.returncode == 2, (result.returncode, result.stderr)
        assert result.stdout == ""
        assert result.stderr.endswith(": Invalid argument\n")

        result = invoke(tool, "--radius", "-1", vertex_path, polygon_path)
        assert result.returncode == 2
        assert result.stdout == ""
        assert result.stderr.startswith("usage: ")

    print("pvr-model-inspect tests passed")


if __name__ == "__main__":
    main()
