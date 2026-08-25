#!/usr/bin/env python3
"""Golden and rejection tests for pvr-model-convert.

Copyright (C) 2026 Joseph Black
"""

import os
import pathlib
import struct
import subprocess
import sys
import tempfile


REPORT = """converted=1
positions=3
texcoords=3
normals=1
triangles=1
strip_records=1
texture_records=1
material_bindings=0
vertex_words=12
polygon_words=29
center_x=0
center_y=0
center_z=0
radius=1.41421354
"""

VERTICES = [
    34 | (10 << 16),
    3 << 16,
    0xBF800000, 0xBF800000, 0,
    0x3F800000, 0xBF800000, 0,
    0, 0x3F800000, 0,
    0xFF,
]

POLYGONS = [
    17, 2, 0xFFFF, 0xFFFF,
    8, 7,
    69, 20, 1, 3,
    0, 0, 0, 0, 0, 0x7FFF,
    1, 1023, 0, 0, 0, 0x7FFF,
    2, 0, 1023, 0, 0, 0x7FFF,
    0xFF,
]


def invoke(*arguments):
    return subprocess.run(
        [*map(str, arguments)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def write_text(path, text):
    path.write_text(text, encoding="ascii")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test.py CONVERTER INSPECTOR")
    converter = sys.argv[1]
    inspector = sys.argv[2]

    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        source = root / "triangle.obj"
        vertices = root / "vertices.bin"
        polygons = root / "polygons.bin"
        write_text(
            source,
            """
# one fully attributed triangle
v -1 -1 0
v 1 -1 0
v 0 1 0
vt 0 0
vt 1 0
vt 0 1
vn 0 0 2
f 1/1/1 2/2/1 3/3/1# trailing comment
""",
        )

        result = invoke(
            converter, "--texture-id", "7", source, vertices, polygons
        )
        assert result.returncode == 0, result.stderr
        assert result.stdout == REPORT, result.stdout
        assert not result.stderr, result.stderr
        assert vertices.read_bytes() == struct.pack("<12I", *VERTICES)
        assert polygons.read_bytes() == struct.pack("<29H", *POLYGONS)

        result = invoke(
            inspector,
            "--center", "0", "0", "0",
            "--radius", "1.41421354",
            vertices,
            polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "triangles=1\n" in result.stdout
        assert "maximum_strip_vertices=3\n" in result.stdout
        assert "texture_references=1\n" in result.stdout
        assert "distinct_textures=1\n" in result.stdout

        original_vertices = vertices.read_bytes()
        original_polygons = polygons.read_bytes()
        result = invoke(converter, source, vertices, polygons)
        assert result.returncode == 1, (
            result.returncode, result.stdout, result.stderr
        )
        assert result.stdout == ""
        assert result.stderr.startswith("textured faces require a resolved ")
        assert vertices.read_bytes() == original_vertices
        assert polygons.read_bytes() == original_polygons

        flipped_vertices = root / "flipped-vertices.bin"
        flipped_polygons = root / "flipped-polygons.bin"
        result = invoke(
            converter,
            "--flip-winding",
            "--flip-v",
            "--texture-id",
            "7",
            source,
            flipped_vertices,
            flipped_polygons,
        )
        assert result.returncode == 0, result.stderr
        expected = POLYGONS.copy()
        expected[10:16] = [0, 0, 1023, 0, 0, 0x7FFF]
        expected[16:22] = [2, 0, 0, 0, 0, 0x7FFF]
        expected[22:28] = [1, 1023, 1023, 0, 0, 0x7FFF]
        assert flipped_polygons.read_bytes() == struct.pack("<29H", *expected)

        relative = root / "relative.obj"
        relative_vertices = root / "relative-vertices.bin"
        relative_polygons = root / "relative-polygons.bin"
        write_text(
            relative,
            """v 0 0 0
v 1 0 0
v 0 1 0
f -3 -2 -1
""",
        )
        result = invoke(
            converter, relative, relative_vertices, relative_polygons
        )
        assert result.returncode == 0, result.stderr
        assert relative_polygons.read_bytes() == struct.pack(
            "<12H", 17, 2, 0xFFFF, 0xFFFF, 64, 5, 1, 3, 0, 1, 2, 0xFF
        )
        result = invoke(
            converter,
            "--texture-id",
            "7",
            relative,
            relative_vertices,
            relative_polygons,
        )
        assert result.returncode == 1
        assert result.stdout == ""

        large_vertices_source = root / "large-vertices.obj"
        large_vertices = root / "large-vertices.bin"
        large_vertices_polygons = root / "large-vertices-polygons.bin"
        write_text(
            large_vertices_source,
            "".join("v 0 0 0\n" for _ in range(21845))
            + "f 1 21844 21845\n",
        )
        result = invoke(
            converter,
            large_vertices_source,
            large_vertices,
            large_vertices_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "positions=21845\n" in result.stdout
        assert "vertex_words=65540\n" in result.stdout
        result = invoke(inspector, large_vertices, large_vertices_polygons)
        assert result.returncode == 0, result.stderr
        assert "vertex_records=2\n" in result.stdout
        assert "maximum_vertex_index=21844\n" in result.stdout

        large_strips_source = root / "large-strips.obj"
        large_strips_vertices = root / "large-strips-vertices.bin"
        large_strips = root / "large-strips.bin"
        write_text(
            large_strips_source,
            """v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
vt 1 0
vt 0 1
vn 0 0 1
""" + "".join(
                "f 1/1/1 2/2/1 3/3/1\n" for _ in range(3450)
            ),
        )
        result = invoke(
            converter,
            "--texture-id",
            "7",
            large_strips_source,
            large_strips_vertices,
            large_strips,
        )
        assert result.returncode == 0, result.stderr
        assert "triangles=3450\n" in result.stdout
        assert "strip_records=2\n" in result.stdout
        assert "polygon_words=65563\n" in result.stdout
        result = invoke(inspector, large_strips_vertices, large_strips)
        assert result.returncode == 0, result.stderr
        assert "strip_records=2\n" in result.stdout
        assert "triangles=3450\n" in result.stdout

        materials_source = root / "materials.obj"
        materials_vertices = root / "materials-vertices.bin"
        materials_polygons = root / "materials-polygons.bin"
        write_text(
            materials_source,
            """mtllib source-materials.mtl
v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
vt 1 0
vt 0 1
usemtl red
f 1/1 2/2 3/3
usemtl red_alias
f 1/1 2/2 3/3
usemtl blue
f 1/1 2/2 3/3
usemtl red
f 1/1 2/2 3/3
""",
        )
        result = invoke(
            converter,
            "--material", "red=2",
            "--material", "red_alias=2",
            "--material", "blue=9",
            materials_source,
            materials_vertices,
            materials_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "triangles=4\n" in result.stdout
        assert "strip_records=3\n" in result.stdout
        assert "texture_records=3\n" in result.stdout
        assert "material_bindings=3\n" in result.stdout
        assert "polygon_words=60\n" in result.stdout
        material_words = struct.unpack(
            "<60H", materials_polygons.read_bytes()
        )
        assert material_words[:9] == (
            17, 2, 0xFFFF, 0xFFFF, 8, 2, 66, 21, 2
        )
        assert material_words[29:32] == (8, 9, 66)
        assert material_words[44:47] == (8, 2, 66)
        assert material_words[59] == 0xFF
        result = invoke(inspector, materials_vertices, materials_polygons)
        assert result.returncode == 0, result.stderr
        assert "texture_references=3\n" in result.stdout
        assert "distinct_textures=2\n" in result.stdout
        assert "strip_records=3\n" in result.stdout
        assert "strips=4\n" in result.stdout

        unresolved = root / "unresolved.obj"
        write_text(
            unresolved,
            """v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
vt 1 0
vt 0 1
usemtl absent
f 1/1 2/2 3/3
""",
        )
        previous_vertices = materials_vertices.read_bytes()
        previous_polygons = materials_polygons.read_bytes()
        result = invoke(
            converter,
            "--material", "present=1",
            unresolved,
            materials_vertices,
            materials_polygons,
        )
        assert result.returncode == 1, (
            result.returncode, result.stdout, result.stderr
        )
        assert result.stdout == ""
        assert result.stderr.startswith(f"{unresolved}:7:")
        assert materials_vertices.read_bytes() == previous_vertices
        assert materials_polygons.read_bytes() == previous_polygons

        result = invoke(
            converter,
            "--material", "red=1",
            "--material", "red=2",
            materials_source,
            materials_vertices,
            materials_polygons,
        )
        assert result.returncode == 2
        assert result.stdout == ""
        assert result.stderr.startswith("usage: ")

        rejected_vertices = root / "rejected-vertices.bin"
        rejected_polygons = root / "rejected-polygons.bin"
        rejected_vertices.write_bytes(b"vertex sentinel")
        rejected_polygons.write_bytes(b"polygon sentinel")
        for name, statement in (
            ("quad", "f 1 2 3 1\n"),
            ("mixed", "f 1/1 2 3\n"),
            ("material", "usemtl opaque\n"),
        ):
            rejected = root / f"{name}.obj"
            write_text(
                rejected,
                """v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
""" + statement,
            )
            result = invoke(
                converter, rejected, rejected_vertices, rejected_polygons
            )
            assert result.returncode == 1, (name, result.stderr)
            assert result.stdout == ""
            assert result.stderr.startswith(f"{rejected}:")
            assert rejected_vertices.read_bytes() == b"vertex sentinel"
            assert rejected_polygons.read_bytes() == b"polygon sentinel"

        result = invoke(converter, source, source, polygons)
        assert result.returncode == 2
        assert result.stdout == ""
        assert result.stderr == "input and output paths must be distinct\n"

        hardlink = root / "source-hardlink.obj"
        os.link(source, hardlink)
        result = invoke(converter, source, hardlink, polygons)
        assert result.returncode == 2
        assert result.stdout == ""
        assert result.stderr == "input and output paths must be distinct\n"

    print("pvr-model-convert tests passed")


if __name__ == "__main__":
    main()
