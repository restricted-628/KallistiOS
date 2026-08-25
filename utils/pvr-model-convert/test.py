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
strips_before=1
strips_after=1
triangles_joined=0
strip_records=1
texture_records=1
material_records=1
material_bindings=0
material_libraries=0
material_definitions=0
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

        joined_source = root / "joined.obj"
        joined_vertices = root / "joined-vertices.bin"
        joined_polygons = root / "joined-polygons.bin"
        write_text(
            joined_source,
            """v 0 0 0
v 1 0 0
v 0 1 0
v 1 1 0
v 0 2 0
v 1 2 0
f 1 2 3
f 3 2 4
f 3 4 5
f 5 4 6
""",
        )
        result = invoke(
            converter,
            "--join-strips",
            joined_source,
            joined_vertices,
            joined_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "triangles=4\n" in result.stdout
        assert "strips_before=4\n" in result.stdout
        assert "strips_after=1\n" in result.stdout
        assert "triangles_joined=3\n" in result.stdout
        assert "polygon_words=15\n" in result.stdout
        assert joined_polygons.read_bytes() == struct.pack(
            "<15H",
            17, 2, 0xFFFF, 0xFFFF,
            64, 8, 1, 6, 0, 1, 2, 3, 4, 5,
            0xFF,
        )
        result = invoke(inspector, joined_vertices, joined_polygons)
        assert result.returncode == 0, result.stderr
        assert "strips=1\n" in result.stdout
        assert "triangles=4\n" in result.stdout
        assert "index_references=6\n" in result.stdout
        assert "maximum_strip_vertices=6\n" in result.stdout

        default_joined_vertices = root / "default-joined-vertices.bin"
        default_joined_polygons = root / "default-joined-polygons.bin"
        result = invoke(
            converter,
            joined_source,
            default_joined_vertices,
            default_joined_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "strips_after=4\n" in result.stdout
        assert "triangles_joined=0\n" in result.stdout
        result = invoke(
            inspector, default_joined_vertices, default_joined_polygons
        )
        assert result.returncode == 0, result.stderr
        assert "strips=4\n" in result.stdout
        assert "triangles=4\n" in result.stdout

        joined_flipped_source = root / "joined-flipped.obj"
        joined_flipped_vertices = root / "joined-flipped-vertices.bin"
        joined_flipped_polygons = root / "joined-flipped-polygons.bin"
        write_text(
            joined_flipped_source,
            """v 0 0 0
v 1 0 0
v 0 1 0
v 1 1 0
f 1 2 3
f 2 4 3
""",
        )
        result = invoke(
            converter,
            "--join-strips",
            "--flip-winding",
            joined_flipped_source,
            joined_flipped_vertices,
            joined_flipped_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "strips_after=1\n" in result.stdout
        assert joined_flipped_polygons.read_bytes() == struct.pack(
            "<13H",
            17, 2, 0xFFFF, 0xFFFF,
            64, 6, 1, 4, 0, 2, 1, 3,
            0xFF,
        )

        attribute_boundary_source = root / "attribute-boundary.obj"
        attribute_boundary_vertices = root / "attribute-boundary-vertices.bin"
        attribute_boundary_polygons = root / "attribute-boundary-polygons.bin"
        write_text(
            attribute_boundary_source,
            """v 0 0 0
v 1 0 0
v 0 1 0
v 1 1 0
vt 0 0
vt 1 0
vt 0 1
vt 1 1
f 1/1 2/2 3/3
f 3/4 2/2 4/4
""",
        )
        result = invoke(
            converter,
            "--join-strips",
            "--texture-id", "4",
            attribute_boundary_source,
            attribute_boundary_vertices,
            attribute_boundary_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "strips_after=2\n" in result.stdout
        assert "triangles_joined=0\n" in result.stdout

        normal_boundary_source = root / "normal-boundary.obj"
        normal_boundary_vertices = root / "normal-boundary-vertices.bin"
        normal_boundary_polygons = root / "normal-boundary-polygons.bin"
        write_text(
            normal_boundary_source,
            """v 0 0 0
v 1 0 0
v 0 1 0
v 1 1 0
vn 0 0 1
vn 0 1 0
f 1//1 2//1 3//1
f 3//2 2//1 4//1
""",
        )
        result = invoke(
            converter,
            "--join-strips",
            normal_boundary_source,
            normal_boundary_vertices,
            normal_boundary_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "strips_after=2\n" in result.stdout
        assert "triangles_joined=0\n" in result.stdout

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

        joined_limit_source = root / "joined-limit.obj"
        joined_limit_vertices = root / "joined-limit-vertices.bin"
        joined_limit_polygons = root / "joined-limit-polygons.bin"
        joined_limit_triangles = 10921
        joined_limit_positions = joined_limit_triangles + 2
        source_parts = []
        source_parts.extend(
            f"v {index} 0 0\n" for index in range(joined_limit_positions)
        )
        source_parts.extend(
            f"vt {index / (joined_limit_positions - 1):.9f} 0\n"
            for index in range(joined_limit_positions)
        )
        source_parts.append("vn 0 0 1\n")
        for triangle in range(joined_limit_triangles):
            if triangle & 1:
                corners = (triangle + 2, triangle + 1, triangle + 3)
            else:
                corners = (triangle + 1, triangle + 2, triangle + 3)
            source_parts.append(
                "f " + " ".join(f"{corner}/{corner}/1" for corner in corners)
                + "\n"
            )
        write_text(joined_limit_source, "".join(source_parts))
        result = invoke(
            converter,
            "--join-strips",
            "--texture-id", "7",
            joined_limit_source,
            joined_limit_vertices,
            joined_limit_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert f"triangles={joined_limit_triangles}\n" in result.stdout
        assert "strips_after=2\n" in result.stdout
        assert "triangles_joined=10919\n" in result.stdout
        assert "strip_records=2\n" in result.stdout
        assert "polygon_words=65565\n" in result.stdout
        result = invoke(
            inspector, joined_limit_vertices, joined_limit_polygons
        )
        assert result.returncode == 0, result.stderr
        assert "strips=2\n" in result.stdout
        assert f"triangles={joined_limit_triangles}\n" in result.stdout
        assert "maximum_strip_vertices=10922\n" in result.stdout

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

        property_library = root / "properties.mtl"
        property_source = root / "properties.obj"
        property_vertices = root / "properties-vertices.bin"
        property_polygons = root / "properties-polygons.bin"
        write_text(
            property_library,
            """newmtl lit
Kd 1 0.5 0
Ka 0.25 0.125 0
Ks 0.1 0.2 0.3
Ns 500
newmtl green
Kd 0 1 0
""",
        )
        write_text(
            property_source,
            """mtllib deliberately-not-loaded.mtl
v 0 0 0
v 1 0 0
v 0 1 0
v 1 1 0
usemtl lit
f 1 2 3
usemtl green
f 3 2 4
""",
        )
        result = invoke(
            converter,
            "--join-strips",
            "--material-library", property_library,
            property_source,
            property_vertices,
            property_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "strips_before=2\n" in result.stdout
        assert "strips_after=2\n" in result.stdout
        assert "material_records=2\n" in result.stdout
        assert "material_libraries=1\n" in result.stdout
        assert "material_definitions=2\n" in result.stdout
        assert "polygon_words=27\n" in result.stdout
        assert property_polygons.read_bytes() == struct.pack(
            "<27H",
            23, 6,
            0x8000, 0xFFFF,
            0x2000, 0xFF40,
            0x334D, 0x081A,
            64, 5, 1, 3, 0, 1, 2,
            17, 2, 0xFF00, 0xFF00,
            64, 5, 1, 3, 2, 1, 3,
            0xFF,
        )
        result = invoke(inspector, property_vertices, property_polygons)
        assert result.returncode == 0, result.stderr
        assert "material_records=2\n" in result.stdout
        assert "strips=2\n" in result.stdout
        assert "triangles=2\n" in result.stdout

        property_textured_source = root / "properties-textured.obj"
        property_textured_vertices = root / "properties-textured-vertices.bin"
        property_textured_polygons = root / "properties-textured-polygons.bin"
        write_text(
            property_textured_source,
            """v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
vt 1 0
vt 0 1
usemtl lit
f 1/1 2/2 3/3
""",
        )
        result = invoke(
            converter,
            "--material", "lit=7",
            "--material-library", property_library,
            property_textured_source,
            property_textured_vertices,
            property_textured_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "texture_records=1\n" in result.stdout
        assert "material_records=1\n" in result.stdout
        assert "material_bindings=1\n" in result.stdout
        assert "material_libraries=1\n" in result.stdout
        result = invoke(
            inspector, property_textured_vertices, property_textured_polygons
        )
        assert result.returncode == 0, result.stderr
        assert "texture_references=1\n" in result.stdout
        assert "material_records=1\n" in result.stdout

        result = invoke(
            converter,
            "--texture-id", "7",
            "--material-library", property_library,
            property_textured_source,
            property_textured_vertices,
            property_textured_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "texture_records=1\n" in result.stdout
        assert "material_records=1\n" in result.stdout
        assert "material_bindings=0\n" in result.stdout

        additional_library = root / "additional.mtl"
        write_text(additional_library, "newmtl blue\nKd 0 0 1\n")
        result = invoke(
            converter,
            "--material-library", property_library,
            "--material-library", additional_library,
            property_source,
            property_vertices,
            property_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "material_libraries=2\n" in result.stdout
        assert "material_definitions=3\n" in result.stdout

        duplicate_name_library = root / "duplicate-name.mtl"
        write_text(duplicate_name_library, "newmtl lit\nKd 1 1 1\n")
        result = invoke(
            converter,
            "--material-library", property_library,
            "--material-library", duplicate_name_library,
            property_source,
            property_vertices,
            property_polygons,
        )
        assert result.returncode == 1
        assert result.stdout == ""
        assert result.stderr.startswith(f"{duplicate_name_library}:1:")

        incomplete_library = root / "incomplete.mtl"
        write_text(incomplete_library, "newmtl lit\nKa 0 0 0\n")
        previous_vertices = property_vertices.read_bytes()
        previous_polygons = property_polygons.read_bytes()
        result = invoke(
            converter,
            "--material-library", incomplete_library,
            property_source,
            property_vertices,
            property_polygons,
        )
        assert result.returncode == 1
        assert result.stdout == ""
        assert result.stderr.startswith(f"{property_source}:6:")
        assert property_vertices.read_bytes() == previous_vertices
        assert property_polygons.read_bytes() == previous_polygons

        result = invoke(
            converter,
            "--material-library", property_library,
            property_source,
            property_library,
            property_polygons,
        )
        assert result.returncode == 2
        assert result.stdout == ""
        assert result.stderr == (
            "material library and output paths must be distinct\n"
        )
        assert property_library.read_text(encoding="ascii").startswith(
            "newmtl lit\n"
        )

        unsupported_library = root / "unsupported.mtl"
        write_text(
            unsupported_library,
            "newmtl lit\nKd 1 1 1\nillum 2\n",
        )
        result = invoke(
            converter,
            "--material-library", unsupported_library,
            property_source,
            property_vertices,
            property_polygons,
        )
        assert result.returncode == 1
        assert result.stdout == ""
        assert result.stderr.startswith(f"{unsupported_library}:3:")
        assert property_vertices.read_bytes() == previous_vertices
        assert property_polygons.read_bytes() == previous_polygons

        duplicate_library = root / "duplicate.mtl"
        write_text(
            duplicate_library,
            "newmtl lit\nKd 1 1 1\nKd 0 0 0\n",
        )
        result = invoke(
            converter,
            "--material-library", duplicate_library,
            property_source,
            property_vertices,
            property_polygons,
        )
        assert result.returncode == 1
        assert result.stdout == ""
        assert result.stderr.startswith(f"{duplicate_library}:3:")
        assert property_vertices.read_bytes() == previous_vertices
        assert property_polygons.read_bytes() == previous_polygons

        material_boundary_source = root / "material-boundary.obj"
        material_boundary_vertices = root / "material-boundary-vertices.bin"
        material_boundary_polygons = root / "material-boundary-polygons.bin"
        write_text(
            material_boundary_source,
            """v 0 0 0
v 1 0 0
v 0 1 0
v 1 1 0
vt 0 0
vt 1 0
vt 0 1
vt 1 1
usemtl first
f 1/1 2/2 3/3
usemtl second
f 3/3 2/2 4/4
""",
        )
        result = invoke(
            converter,
            "--join-strips",
            "--material", "first=1",
            "--material", "second=2",
            material_boundary_source,
            material_boundary_vertices,
            material_boundary_polygons,
        )
        assert result.returncode == 0, result.stderr
        assert "strips_after=2\n" in result.stdout
        assert "triangles_joined=0\n" in result.stdout
        assert "texture_records=2\n" in result.stdout

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
