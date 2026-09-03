#!/usr/bin/env python3
"""Golden and rejection tests for pvr-model-convert.

Copyright (C) 2026 Joseph Black
"""

import os
import base64
import json
import math
import pathlib
import shlex
import struct
import subprocess
import sys
import tempfile
import zlib


REPORT = """converted=1
positions=3
texcoords=3
normals=1
triangles=1
strips_before=1
strips_after=1
triangles_joined=0
strip_records=1
maximum_strip_vertices=3
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
    17 | (8 << 8), 2, 0xFFFF, 0xFFFF,
    8, 7,
    79, 20, 1, 3,
    0, 0, 0, 0, 0, 0x7FFF,
    1, 1024, 0, 0, 0, 0x7FFF,
    2, 0, 1024, 0, 0, 0x7FFF,
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


def png_rgba(width, height, pixels):
    assert len(pixels) == width * height * 4

    def chunk(kind, payload):
        body = kind + payload
        return (struct.pack(">I", len(payload)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    rows = b"".join(
        b"\0" + pixels[row * width * 4:(row + 1) * width * 4]
        for row in range(height)
    )
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                        8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows)) +
            chunk(b"IEND", b""))


def compile_embedded(generated, root):
    repository = pathlib.Path(__file__).resolve().parents[2]
    verifier_source = root / "embedded-verifier.c"
    verifier = root / "embedded-verifier"
    write_text(
        verifier_source,
        """#include <dc/pvr_chunk_model.h>
#include <math.h>

extern const pvr_chunk_model_t test_model;

int main(void) {
    if(test_model.vertex_word_count != 12u ||
       test_model.polygon_word_count != 29u ||
       test_model.vertex_words[0] != (34u | (10u << 16)) ||
       test_model.vertex_words[11] != 0xffu ||
       test_model.polygon_words[0] != (17u | (8u << 8)) ||
       test_model.polygon_words[28] != 0xffu ||
       fabsf(test_model.radius - 1.41421354f) > 0.000001f)
        return 1;
    return 0;
}
""",
    )
    command = [
        *shlex.split(os.environ.get("CC", "cc")),
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-I", str(repository / "utils/pvr-model-convert/include"),
        "-I", str(repository / "kernel/arch/dreamcast/include"),
        str(generated),
        str(verifier_source),
        "-lm",
        "-o", str(verifier),
    ]
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    result = invoke(verifier)
    assert result.returncode == 0, (result.stdout, result.stderr)


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

        signed_source = root / "signed-uv.obj"
        signed_vertices = root / "signed-uv-vertices.bin"
        signed_polygons = root / "signed-uv-polygons.bin"
        write_text(
            signed_source,
            """v 0 0 0
v 1 0 0
v 0 1 0
vt -2.5 0
vt 31.5 0
vt 0 1
f 1/1 2/2 3/3
""",
        )
        result = invoke(
            converter, "--texture-id", "7", signed_source,
            signed_vertices, signed_polygons,
        )
        assert result.returncode == 0, result.stderr
        signed_words = struct.unpack(
            f"<{len(signed_polygons.read_bytes()) // 2}H",
            signed_polygons.read_bytes(),
        )
        assert signed_words[6] == 77
        assert signed_words[11:13] == ((-2560) & 0xFFFF, 0)
        assert signed_words[14:16] == (32256, 0)

        seam_source = root / "attribute-seam.obj"
        seam_vertices = root / "attribute-seam-vertices.bin"
        seam_polygons = root / "attribute-seam-polygons.bin"
        write_text(
            seam_source,
            """v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
vt 0 0
vt 1 0
vt 1 1
vt 0.5 0.25
vt 0.25 0.5
vt 0 1
vn 0 0 1
vn 0 1 0
f 1/1/1 2/2/1 3/3/1
f 1/4/2 3/5/2 4/6/2
""",
        )
        result = invoke(
            converter, "--texture-id", "7", seam_source,
            seam_vertices, seam_polygons,
        )
        assert result.returncode == 0, result.stderr
        seam_words = struct.unpack(
            f"<{len(seam_polygons.read_bytes()) // 2}H",
            seam_polygons.read_bytes(),
        )
        assert seam_words[6:10] == (79, 39, 2, 3)
        assert seam_words[10] == seam_words[29] == 0
        assert seam_words[11:13] == (0, 0)
        assert seam_words[30:32] == (512, 256)
        assert seam_words[13:16] == (0, 0, 0x7FFF)
        assert seam_words[32:35] == (0, 0x7FFF, 0)

        wide_source = root / "wide-uv.obj"
        wide_vertices = root / "wide-uv-vertices.bin"
        wide_polygons = root / "wide-uv-polygons.bin"
        write_text(
            wide_source,
            """v 0 0 0
v 1 0 0
v 0 1 0
vt 70 0
vt 71 0
vt 70 1
f 1/1 2/2 3/3
""",
        )
        result = invoke(
            converter, "--texture-id", "7", wide_source,
            wide_vertices, wide_polygons,
        )
        assert result.returncode == 0, result.stderr
        wide_words = struct.unpack(
            f"<{len(wide_polygons.read_bytes()) // 2}H",
            wide_polygons.read_bytes(),
        )
        assert wide_words[6] == 76
        assert wide_words[11:13] == (17920, 0)

        float_source = root / "float-uv.obj"
        float_vertices = root / "float-uv-vertices.bin"
        float_polygons = root / "float-uv-polygons.bin"
        write_text(
            float_source,
            """v 0 0 0
v 1 0 0
v 0 1 0
vt 200.25 -70.5
vt 201.25 -70.5
vt 200.25 -69.5
vn 0 0 1
f 1/1/1 2/2/1 3/3/1
""",
        )
        result = invoke(
            converter, "--texture-id", "7", float_source,
            float_vertices, float_polygons,
        )
        assert result.returncode == 0, result.stderr
        float_words = struct.unpack(
            f"<{len(float_polygons.read_bytes()) // 2}H",
            float_polygons.read_bytes(),
        )
        assert float_words[6] == 85
        assert struct.unpack_from("<ff", float_polygons.read_bytes(),
                                  22) == (200.25, -70.5)

        generated = root / "test-model.c"
        result = invoke(
            converter,
            "--texture-id", "7",
            "--emit-c", "test_model",
            source,
            generated,
        )
        assert result.returncode == 0, result.stderr
        assert result.stdout == REPORT + "c_symbol=test_model\n"
        generated_text = generated.read_text(encoding="ascii")
        assert "const pvr_chunk_model_t test_model = {" in generated_text
        assert "alignas(uint32_t) static const uint32_t " in generated_text
        assert "alignas(uint16_t) static const uint16_t " in generated_text
        assert ".center = { 0x0p+0F, 0x0p+0F, 0x0p+0F }," in generated_text
        assert ".radius = 0x1.6a09e6p+0F" in generated_text
        assert str(source) not in generated_text
        compile_embedded(generated, root)

        raw_asset = root / "triangle-raw.pcm"
        result = invoke(
            converter,
            "--texture-id", "7",
            "--emit-asset",
            source,
            raw_asset,
        )
        assert result.returncode == 0, result.stderr
        raw_bytes = raw_asset.read_bytes()
        assert result.stdout == (
            REPORT + f"asset_bytes={len(raw_bytes)}\nvertex_codec=raw\n"
        )
        assert raw_bytes[:4] == b"PCM1"
        assert struct.unpack_from("<HHI", raw_bytes, 4) == (
            1, 96, len(raw_bytes)
        )
        assert zlib.crc32(raw_bytes[:80]) == struct.unpack_from(
            "<I", raw_bytes, 80
        )[0]
        vertex_offset, vertex_stored, vertex_decoded, vertex_crc = (
            struct.unpack_from("<4I", raw_bytes, 32)
        )
        polygon_offset, polygon_stored, polygon_decoded, polygon_crc = (
            struct.unpack_from("<4I", raw_bytes, 56)
        )
        assert struct.unpack_from("<H", raw_bytes, 48)[0] == 0
        assert struct.unpack_from("<H", raw_bytes, 72)[0] == 0
        assert vertex_stored == vertex_decoded == len(vertices.read_bytes())
        assert polygon_stored == polygon_decoded == len(polygons.read_bytes())
        assert raw_bytes[vertex_offset:vertex_offset + vertex_stored] == (
            vertices.read_bytes()
        )
        assert raw_bytes[polygon_offset:polygon_offset + polygon_stored] == (
            polygons.read_bytes()
        )
        assert vertex_crc == zlib.crc32(vertices.read_bytes())
        assert polygon_crc == zlib.crc32(polygons.read_bytes())

        directory_asset = root / "triangle-directory.pcm"
        result = invoke(
            converter,
            "--texture-id", "7",
            "--emit-asset",
            "--section-directory",
            source,
            directory_asset,
        )
        assert result.returncode == 0, result.stderr
        directory_bytes = directory_asset.read_bytes()
        assert result.stdout == (
            REPORT + f"asset_bytes={len(directory_bytes)}\n"
            "vertex_codec=raw\nasset_container=pcm2\n"
        )
        assert directory_bytes[:4] == b"PCM2"
        assert struct.unpack_from("<HHI", directory_bytes, 4) == (
            2, 64, len(directory_bytes)
        )
        assert zlib.crc32(directory_bytes[:60]) == struct.unpack_from(
            "<I", directory_bytes, 60
        )[0]
        section_count, directory_offset, directory_size, directory_crc = (
            struct.unpack_from("<4I", directory_bytes, 32)
        )
        assert (section_count, directory_offset, directory_size) == (
            4, 64, 128
        )
        assert zlib.crc32(
            directory_bytes[directory_offset:directory_offset + directory_size]
        ) == directory_crc
        vertex_descriptor = struct.unpack_from("<7IHH", directory_bytes, 64)
        polygon_descriptor = struct.unpack_from("<7IHH", directory_bytes, 96)
        resource_descriptor = struct.unpack_from("<7IHH", directory_bytes, 128)
        model_table_descriptor = struct.unpack_from(
            "<7IHH", directory_bytes, 160
        )
        assert vertex_descriptor[0] == 1 and vertex_descriptor[1] == 0
        assert polygon_descriptor[0] == 2 and polygon_descriptor[1] == 0
        assert resource_descriptor[0] == 3 and resource_descriptor[1] == 0
        assert model_table_descriptor[0] == 12 and \
               model_table_descriptor[1] == 0
        assert vertex_descriptor[7:] == (0, 4)
        assert polygon_descriptor[7:] == (0, 2)
        assert resource_descriptor[7:] == (0, 4)
        assert model_table_descriptor[7:] == (0, 4)
        assert directory_bytes[
            vertex_descriptor[2]:vertex_descriptor[2] + vertex_descriptor[3]
        ] == vertices.read_bytes()
        assert directory_bytes[
            polygon_descriptor[2]:polygon_descriptor[2] + polygon_descriptor[3]
        ] == polygons.read_bytes()
        resource = directory_bytes[
            resource_descriptor[2]:resource_descriptor[2] +
            resource_descriptor[3]
        ]
        assert resource[:4] == b"PRT1"
        assert struct.unpack_from("<HHIIHHI", resource, 4) == (
            1, 48, 56, 1, 8, 0, 8
        )
        assert struct.unpack_from("<HHI", resource, 48) == (7, 1, 0)
        assert zlib.crc32(resource[48:]) == struct.unpack_from(
            "<I", resource, 24
        )[0]
        assert zlib.crc32(resource[:44]) == struct.unpack_from(
            "<I", resource, 44
        )[0]
        model_table = directory_bytes[
            model_table_descriptor[2]:model_table_descriptor[2] +
            model_table_descriptor[3]
        ]
        assert model_table[:4] == b"PMT1"
        assert struct.unpack_from("<HHIIH", model_table, 4) == (
            2, 32, 96, 1, 64
        )
        assert zlib.crc32(model_table[32:]) == struct.unpack_from(
            "<I", model_table, 20
        )[0]
        assert zlib.crc32(model_table[:28]) == struct.unpack_from(
            "<I", model_table, 28
        )[0]
        assert struct.unpack_from("<10I", model_table, 32) == (
            0, 0, 0, 0xffffffff, 0xffffffff, 0xffffffff,
            0xffffffff, 0xffffffff, 0xffffffff, 0,
        )
        cooked_asset = root / "triangle-cooked.pcm"
        result = invoke(
            converter,
            "--texture-id", "7",
            "--emit-asset",
            "--section-directory",
            "--cooked-cache",
            source,
            cooked_asset,
        )
        assert result.returncode == 0, result.stderr
        cooked_asset_bytes = cooked_asset.read_bytes()
        assert struct.unpack_from("<I", cooked_asset_bytes, 32)[0] == 5
        cooked_descriptor = struct.unpack_from(
            "<7IHH", cooked_asset_bytes, 64 + 3 * 32
        )
        assert cooked_descriptor[0] == 10
        assert cooked_descriptor[1] == 0
        assert cooked_descriptor[7:] == (0, 4)
        cooked = cooked_asset_bytes[
            cooked_descriptor[2]:cooked_descriptor[2] + cooked_descriptor[3]
        ]
        assert cooked[:4] == b"PCC1"
        assert struct.unpack_from("<HHIHH4I", cooked, 4) == (
            1, 128, len(cooked), 1, 0, 1, 3, 3, 0
        )
        assert zlib.crc32(cooked[128:]) == struct.unpack_from(
            "<I", cooked, 84
        )[0]
        assert zlib.crc32(cooked[:124]) == struct.unpack_from(
            "<I", cooked, 124
        )[0]

        scene_asset = root / "triangle-scene.pcm"
        result = invoke(
            converter,
            "--texture-id", "7",
            "--emit-asset",
            "--section-directory",
            "--scene-root",
            source,
            scene_asset,
        )
        assert result.returncode == 0, result.stderr
        scene_bytes = scene_asset.read_bytes()
        assert result.stdout == (
            REPORT + f"asset_bytes={len(scene_bytes)}\n"
            "vertex_codec=raw\nasset_container=pcm2\n"
            "hierarchy_nodes=1\n"
        )
        assert struct.unpack_from("<I", scene_bytes, 32)[0] == 5
        hierarchy_descriptor = struct.unpack_from(
            "<7IHH", scene_bytes, 64 + 3 * 32
        )
        assert hierarchy_descriptor[0] == 8
        assert hierarchy_descriptor[7:] == (0, 4)
        hierarchy_offset = hierarchy_descriptor[2]
        hierarchy_size = hierarchy_descriptor[3]
        hierarchy = scene_bytes[
            hierarchy_offset:hierarchy_offset + hierarchy_size
        ]
        assert hierarchy[:4] == b"PCH1"
        assert struct.unpack_from("<HHIIH", hierarchy, 4) == (
            2, 32, 112, 1, 80
        )
        assert zlib.crc32(hierarchy[32:]) == struct.unpack_from(
            "<I", hierarchy, 20
        )[0]
        assert zlib.crc32(hierarchy[:28]) == struct.unpack_from(
            "<I", hierarchy, 28
        )[0]
        assert struct.unpack_from("<II", hierarchy, 32) == (
            0xFFFFFFFF, 0
        )
        assert struct.unpack_from("<16f", hierarchy, 48) == (
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0,
        )

        skin_asset = root / "triangle-scene-skin.pcm"
        result = invoke(
            converter,
            "--texture-id", "7",
            "--emit-asset",
            "--section-directory",
            "--scene-root",
            "--rigid-skin",
            "--morph-target", "1.5", "-2.0", "0.25",
            "--animation-offset", "3.0", "4.0", "5.0",
            source,
            skin_asset,
        )
        assert result.returncode == 0, result.stderr
        skin_asset_bytes = skin_asset.read_bytes()
        assert result.stdout == (
            REPORT + f"asset_bytes={len(skin_asset_bytes)}\n"
            "vertex_codec=raw\nasset_container=pcm2\n"
            "hierarchy_nodes=1\n"
            "general_skin_spans=3\n"
            "general_skin_weights=3\n"
            "skeleton_joints=1\n"
            "morph_targets=1\n"
            "morph_deltas=1\n"
            "animation_transforms=1\n"
            "animation_tracks=1\n"
            "animation_keys=2\n"
        )
        assert struct.unpack_from("<I", skin_asset_bytes, 32)[0] == 9
        skin_descriptor = struct.unpack_from(
            "<7IHH", skin_asset_bytes, 64 + 4 * 32
        )
        assert skin_descriptor[0] == 6
        assert skin_descriptor[7:] == (0, 4)
        skin_offset = skin_descriptor[2]
        skin_size = skin_descriptor[3]
        skin = skin_asset_bytes[skin_offset:skin_offset + skin_size]
        assert skin[:4] == b"PSG1"
        assert struct.unpack_from("<HHIIIIHHII", skin, 4) == (
            1, 48, 84, 3, 3, 1, 8, 4, 24, 12
        )
        assert zlib.crc32(skin[48:]) == struct.unpack_from(
            "<I", skin, 36
        )[0]
        assert zlib.crc32(skin[:44]) == struct.unpack_from(
            "<I", skin, 44
        )[0]
        assert tuple(
            struct.unpack_from("<HHI", skin, 48 + index * 8)
            for index in range(3)
        ) == ((0, 1, 0), (1, 1, 1), (2, 1, 2))
        assert tuple(
            struct.unpack_from("<HH", skin, 72 + index * 4)
            for index in range(3)
        ) == ((0, 65535), (0, 65535), (0, 65535))

        skeleton_descriptor = struct.unpack_from(
            "<7IHH", skin_asset_bytes, 64 + 5 * 32
        )
        assert skeleton_descriptor[0] == 11
        assert skeleton_descriptor[7:] == (0, 4)
        skeleton_offset = skeleton_descriptor[2]
        skeleton_size = skeleton_descriptor[3]
        skeleton = skin_asset_bytes[
            skeleton_offset:skeleton_offset + skeleton_size
        ]
        assert skeleton[:4] == b"PSK1"
        assert struct.unpack_from("<HHIIIHHI", skeleton, 4) == (
            1, 48, 128, 1, 1, 80, 64, 80
        )
        assert zlib.crc32(skeleton[48:]) == struct.unpack_from(
            "<I", skeleton, 28
        )[0]
        assert zlib.crc32(skeleton[:44]) == struct.unpack_from(
            "<I", skeleton, 44
        )[0]
        assert struct.unpack_from("<4I", skeleton, 48) == (0, 0, 0, 0)
        assert struct.unpack_from("<16f", skeleton, 64) == (
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0,
        )

        shape_descriptor = struct.unpack_from(
            "<7IHH", skin_asset_bytes, 64 + 6 * 32
        )
        assert shape_descriptor[0] == 7
        assert shape_descriptor[7:] == (0, 4)
        shape_offset = shape_descriptor[2]
        shape_size = shape_descriptor[3]
        shape = skin_asset_bytes[shape_offset:shape_offset + shape_size]
        assert shape[:4] == b"PMS1"
        assert struct.unpack_from("<HHIIIHHII", shape, 4) == (
            1, 48, 84, 1, 1, 8, 28, 8, 28
        )
        assert zlib.crc32(shape[48:]) == struct.unpack_from(
            "<I", shape, 32
        )[0]
        assert zlib.crc32(shape[:44]) == struct.unpack_from(
            "<I", shape, 44
        )[0]
        assert struct.unpack_from("<II", shape, 48) == (0, 1)
        assert struct.unpack_from("<HH6f", shape, 56) == (
            0, 0, 1.5, -2.0, 0.25, 0.0, 0.0, 0.0
        )

        animation_descriptor = struct.unpack_from(
            "<7IHH", skin_asset_bytes, 64 + 7 * 32
        )
        assert animation_descriptor[0] == 9
        assert animation_descriptor[7:] == (0, 4)
        animation_offset = animation_descriptor[2]
        animation_size = animation_descriptor[3]
        animation = skin_asset_bytes[
            animation_offset:animation_offset + animation_size
        ]
        assert animation[:4] == b"PAT1"
        assert struct.unpack_from("<HHIIII", animation, 4) == (
            3, 64, 256, 1, 1, 2
        )
        assert struct.unpack_from("<HHHHIIIff", animation, 24) == (
            64, 16, 56, 0, 64, 16, 112, 0.0, 1.0
        )
        assert zlib.crc32(animation[64:]) == struct.unpack_from(
            "<I", animation, 52
        )[0]
        assert zlib.crc32(animation[:60]) == struct.unpack_from(
            "<I", animation, 60
        )[0]
        assert struct.unpack_from("<4I3f4f3f2I", animation, 64) == (
            0, 0xffffffff, 0xffffffff, 0xffffffff,
            0.0, 0.0, 0.0,
            1.0, 0.0, 0.0, 0.0,
            1.0, 1.0, 1.0,
            1, 0,
        )
        assert struct.unpack_from("<HHIII", animation, 128) == (
            1, 1, 0, 2, 0
        )
        assert struct.unpack_from("<5fI", animation, 144) == (
            0.0, 0.0, 0.0, 0.0, 1.0, 0
        )
        assert struct.unpack_from("<5fI", animation, 200) == (
            1.0, 3.0, 4.0, 5.0, 1.0, 0
        )

        gltf_binary = (
            struct.pack(
                "<9f",
                -1.0, -1.0, 0.0,
                1.0, -1.0, 0.0,
                0.0, 1.0, 0.0,
            ) +
            struct.pack("<9f", *(0.0, 0.0, 1.0) * 3) +
            struct.pack("<3H", 0, 1, 2) + b"\0\0"
        )
        gltf_source = root / "triangle.gltf"
        gltf_source.write_text(json.dumps({
            "asset": {"version": "2.0"},
            "buffers": [{
                "byteLength": len(gltf_binary),
                "uri": "data:application/octet-stream;base64," +
                       base64.b64encode(gltf_binary).decode("ascii"),
            }],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 36, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 72, "byteLength": 6,
                 "target": 34963},
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 1, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 2, "componentType": 5123, "count": 3,
                 "type": "SCALAR"},
            ],
            "materials": [{
                "name": "mat",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.25, 0.5, 0.75, 1.0]
                },
            }],
            "meshes": [{"primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1},
                "indices": 2,
                "material": 0,
            }]}],
            "nodes": [
                {"name": "root", "translation": [2.0, 0.0, 0.0],
                 "children": [1]},
                {"name": "mesh", "mesh": 0,
                 "translation": [0.0, 3.0, 0.0]},
            ],
            "scenes": [{"nodes": [0]}],
            "scene": 0,
        }), encoding="utf-8")
        gltf_asset = root / "triangle-gltf.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            gltf_source, gltf_asset,
        )
        assert result.returncode == 0, result.stderr
        assert "positions=3\n" in result.stdout
        assert "normals=3\n" in result.stdout
        assert "triangles=1\n" in result.stdout
        assert "material_definitions=2\n" in result.stdout
        assert "hierarchy_nodes=2\n" in result.stdout
        gltf_asset_bytes = gltf_asset.read_bytes()
        assert struct.unpack_from("<I", gltf_asset_bytes, 32)[0] == 4
        gltf_hierarchy_descriptor = struct.unpack_from(
            "<7IHH", gltf_asset_bytes, 64 + 2 * 32
        )
        assert gltf_hierarchy_descriptor[0] == 8
        gltf_hierarchy_offset = gltf_hierarchy_descriptor[2]
        gltf_hierarchy_size = gltf_hierarchy_descriptor[3]
        gltf_hierarchy = gltf_asset_bytes[
            gltf_hierarchy_offset:
            gltf_hierarchy_offset + gltf_hierarchy_size
        ]
        assert gltf_hierarchy[:4] == b"PCH1"
        assert struct.unpack_from("<H", gltf_hierarchy, 4)[0] == 2
        assert struct.unpack_from("<I", gltf_hierarchy, 12)[0] == 2
        assert struct.unpack_from("<II", gltf_hierarchy, 32) == (
            0xffffffff, 0xffffffff
        )
        assert struct.unpack_from("<II", gltf_hierarchy, 112) == (0, 0)
        assert struct.unpack_from("<4f", gltf_hierarchy, 96) == (
            2.0, 0.0, 0.0, 1.0
        )
        assert struct.unpack_from("<4f", gltf_hierarchy, 176) == (
            0.0, 3.0, 0.0, 1.0
        )

        color_binary = (
            struct.pack(
                "<9f",
                -1.0, -1.0, 0.0,
                1.0, -1.0, 0.0,
                0.0, 1.0, 0.0,
            ) +
            struct.pack("<12f",
                        1.0, 0.0, 0.0, 0.5,
                        0.0, 1.0, 0.0, 1.0,
                        0.0, 0.0, 1.0, 1.0) +
            struct.pack("<9f", *(0.0, 0.0, 1.0) * 3) +
            struct.pack("<3H", 0, 1, 2) + b"\0\0"
        )
        color_source = root / "triangle-color.gltf"
        color_document = {
            "asset": {"version": "2.0"},
            "buffers": [{
                "byteLength": len(color_binary),
                "uri": "data:application/octet-stream;base64," +
                       base64.b64encode(color_binary).decode("ascii"),
            }],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 36, "byteLength": 48,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 84, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 120, "byteLength": 6,
                 "target": 34963},
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 1, "componentType": 5126, "count": 3,
                 "type": "VEC4"},
                {"bufferView": 2, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 3, "componentType": 5123, "count": 3,
                 "type": "SCALAR"},
            ],
            "materials": [{
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.5, 0.5, 0.5, 1.0]
                },
            }],
            "meshes": [{"primitives": [{
                "attributes": {
                    "POSITION": 0, "COLOR_0": 1, "NORMAL": 2
                },
                "indices": 3,
                "material": 0,
            }]}],
            "nodes": [{"mesh": 0}],
            "scenes": [{"nodes": [0]}],
            "scene": 0,
        }
        color_source.write_text(json.dumps(color_document), encoding="utf-8")
        color_asset = root / "triangle-color.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            color_source, color_asset,
        )
        assert result.returncode == 0, result.stderr
        color_asset_bytes = color_asset.read_bytes()
        color_vertex_descriptor = struct.unpack_from(
            "<7IHH", color_asset_bytes, 64
        )
        color_vertices = color_asset_bytes[
            color_vertex_descriptor[2]:
            color_vertex_descriptor[2] + color_vertex_descriptor[3]
        ]
        assert len(color_vertices) == 60
        assert struct.unpack_from("<I", color_vertices, 0)[0] == (
            35 | (13 << 16)
        )
        assert struct.unpack_from("<I", color_vertices, 20)[0] == 0x80800000
        assert struct.unpack_from("<I", color_vertices, 36)[0] == 0xff008000
        assert struct.unpack_from("<I", color_vertices, 52)[0] == 0xff000080

        color3_binary = (
            color_binary[:36] +
            struct.pack("<9f",
                        1.0, 0.0, 0.0,
                        0.0, 1.0, 0.0,
                        0.0, 0.0, 1.0) +
            struct.pack("<9f", *(0.0, 0.0, 1.0) * 3) +
            struct.pack("<3H", 0, 1, 2) + b"\0\0"
        )
        color3_document = json.loads(json.dumps(color_document))
        color3_document["buffers"][0]["byteLength"] = len(color3_binary)
        color3_document["buffers"][0]["uri"] = (
            "data:application/octet-stream;base64," +
            base64.b64encode(color3_binary).decode("ascii")
        )
        color3_document["bufferViews"][1]["byteLength"] = 36
        color3_document["bufferViews"][2]["byteOffset"] = 72
        color3_document["bufferViews"][3]["byteOffset"] = 108
        color3_document["accessors"][1]["type"] = "VEC3"
        color3_source = root / "triangle-color3.gltf"
        color3_source.write_text(
            json.dumps(color3_document), encoding="utf-8"
        )
        color3_asset = root / "triangle-color3.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            color3_source, color3_asset,
        )
        assert result.returncode == 0, result.stderr
        color3_bytes = color3_asset.read_bytes()
        color3_vertex_descriptor = struct.unpack_from(
            "<7IHH", color3_bytes, 64
        )
        color3_vertices = color3_bytes[
            color3_vertex_descriptor[2]:
            color3_vertex_descriptor[2] + color3_vertex_descriptor[3]
        ]
        assert struct.unpack_from("<I", color3_vertices, 20)[0] == 0xff800000

        multi_gltf_binary = (
            gltf_binary +
            struct.pack("<6f", 0.0, 0.0, 1.0, 0.0, 0.5, 1.0) +
            struct.pack(
                "<9f", 0.0, 0.0, 0.0, 0.25, 0.0, 0.0,
                0.0, 0.0, 0.0,
            ) +
            struct.pack(
                "<9f", 0.0, 0.0, 0.0, 0.0, 0.5, 0.0,
                0.0, 0.0, 0.0,
            ) +
            bytes((0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0)) +
            struct.pack(
                "<12f", 1.0, 0.0, 0.0, 0.0,
                1.0, 0.0, 0.0, 0.0,
                0.25, 0.75, 0.0, 0.0,
            ) +
            struct.pack(
                "<32f", *(
                    (1.0, 0.0, 0.0, 0.0,
                     0.0, 1.0, 0.0, 0.0,
                     0.0, 0.0, 1.0, 0.0,
                     0.0, 0.0, 0.0, 1.0) * 2
                ),
            )
        )
        texture_pixels = bytes(
            channel
            for y in range(8)
            for x in range(8)
            for channel in ((255, 0, 0, 255) if (x + y) & 1
                            else (0, 255, 0, 255))
        )
        texture_png = png_rgba(8, 8, texture_pixels)
        multi_gltf_source = root / "two-meshes.gltf"
        multi_gltf_source.write_text(json.dumps({
            "asset": {"version": "2.0"},
            "buffers": [{
                "byteLength": len(multi_gltf_binary),
                "uri": "data:application/octet-stream;base64," +
                       base64.b64encode(multi_gltf_binary).decode("ascii"),
            }],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 36, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 72, "byteLength": 6,
                 "target": 34963},
                {"buffer": 0, "byteOffset": 80, "byteLength": 24,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 104, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 140, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 176, "byteLength": 12,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 188, "byteLength": 48,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 236, "byteLength": 128},
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 1, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 2, "componentType": 5123, "count": 3,
                 "type": "SCALAR"},
                {"bufferView": 3, "componentType": 5126, "count": 3,
                 "type": "VEC2"},
                {"bufferView": 4, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 5, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 6, "componentType": 5121, "count": 3,
                 "type": "VEC4"},
                {"bufferView": 7, "componentType": 5126, "count": 3,
                 "type": "VEC4"},
                {"bufferView": 8, "componentType": 5126, "count": 2,
                 "type": "MAT4"},
            ],
            "images": [{
                "uri": "data:image/png;base64," +
                       base64.b64encode(texture_png).decode("ascii")
            }],
            "textures": [{"source": 0}],
            "materials": [{
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.25, 0.5, 0.75, 0.5],
                    "baseColorTexture": {"index": 0},
                },
                "alphaMode": "BLEND",
                "doubleSided": True,
            }, {
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.25, 0.5, 0.75, 1.0],
                    "baseColorTexture": {"index": 0},
                },
                "alphaMode": "MASK",
            }],
            "meshes": [
                {"primitives": [{
                    "attributes": {
                        "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 3,
                        "JOINTS_0": 6, "WEIGHTS_0": 7,
                    },
                    "indices": 2, "material": 0,
                    "targets": [{"POSITION": 4}],
                }]},
                {"primitives": [{
                    "attributes": {
                        "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 3,
                        "JOINTS_0": 6, "WEIGHTS_0": 7,
                    },
                    "indices": 2, "material": 1,
                    "targets": [{"POSITION": 5}],
                }]},
            ],
            "skins": [{"joints": [3, 4], "inverseBindMatrices": 8}],
            "nodes": [
                {"children": [1, 2, 3]},
                {"mesh": 0, "skin": 0,
                 "translation": [-2.0, 0.0, 0.0]},
                {"mesh": 1, "skin": 0,
                 "translation": [2.0, 0.0, 0.0]},
                {"children": [4]},
                {},
            ],
            "scenes": [{"nodes": [0]}],
            "scene": 0,
        }), encoding="utf-8")
        multi_gltf_asset = root / "two-meshes.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            "--cooked-cache",
            multi_gltf_source, multi_gltf_asset,
        )
        assert result.returncode == 0, result.stderr
        assert "models=2\n" in result.stdout
        assert "hierarchy_nodes=5\n" in result.stdout
        assert "general_skin_spans=6\n" in result.stdout
        assert "general_skin_weights=8\n" in result.stdout
        assert "skeleton_joints=4\n" in result.stdout
        assert "morph_targets=2\n" in result.stdout
        assert "morph_deltas=2\n" in result.stdout
        multi_bytes = multi_gltf_asset.read_bytes()
        assert struct.unpack_from("<I", multi_bytes, 32)[0] == 17
        multi_descriptors = [
            struct.unpack_from("<7IHH", multi_bytes, 64 + i * 32)
            for i in range(17)
        ]
        assert [descriptor[0] for descriptor in multi_descriptors] == [
            1, 2, 3, 10, 6, 11, 7,
            1, 2, 3, 10, 6, 11, 7,
            8, 12, 15,
        ]
        first_polygon = multi_bytes[
            multi_descriptors[1][2]:
            multi_descriptors[1][2] + multi_descriptors[1][3]
        ]
        second_polygon = multi_bytes[
            multi_descriptors[8][2]:
            multi_descriptors[8][2] + multi_descriptors[8][3]
        ]
        first_polygon_words = struct.unpack(
            f"<{len(first_polygon) // 2}H", first_polygon
        )
        second_polygon_words = struct.unpack(
            f"<{len(second_polygon) // 2}H", second_polygon
        )
        assert first_polygon_words[0] == 23 | (0x25 << 8)
        assert first_polygon_words[3] >> 8 == 0x80
        assert first_polygon_words[10] == 79 | (0x18 << 8)
        assert second_polygon_words[0] == 23 | (0x08 << 8)
        assert second_polygon_words[3] >> 8 == 0xFF
        assert second_polygon_words[10] == 79 | (0x08 << 8)
        multi_table = multi_bytes[
            multi_descriptors[15][2]:
            multi_descriptors[15][2] + multi_descriptors[15][3]
        ]
        assert multi_table[:4] == b"PMT1"
        assert struct.unpack_from("<I", multi_table, 12)[0] == 2
        assert struct.unpack_from("<3I", multi_table, 32) == (
            0, 0, 0
        )
        assert struct.unpack_from("<3I", multi_table, 96) == (
            1, 1, 1
        )
        assert struct.unpack_from("<I", multi_table, 64)[0] == 0
        assert struct.unpack_from("<I", multi_table, 128)[0] == 1
        assert struct.unpack_from("<I", multi_table, 60)[0] == 0
        assert struct.unpack_from("<I", multi_table, 124)[0] == 1
        assert struct.unpack_from("<II", multi_table, 52) == (0, 0)
        assert struct.unpack_from("<II", multi_table, 116) == (1, 1)
        multi_hierarchy = multi_bytes[
            multi_descriptors[14][2]:
            multi_descriptors[14][2] + multi_descriptors[14][3]
        ]
        assert struct.unpack_from("<I", multi_hierarchy, 12)[0] == 5
        assert struct.unpack_from("<II", multi_hierarchy, 112) == (0, 0)
        assert struct.unpack_from("<II", multi_hierarchy, 192) == (0, 1)
        multi_texture = multi_bytes[
            multi_descriptors[16][2]:
            multi_descriptors[16][2] + multi_descriptors[16][3]
        ]
        assert multi_texture[:4] == b"PTX1"
        assert struct.unpack_from("<HHIIHHI", multi_texture, 4) == (
            1, 64, len(multi_texture), 1, 32, 0, 96
        )
        assert struct.unpack_from("<HBBHHHHIII", multi_texture, 64) == (
            0, 1, 0, 8, 8, 0, 0, 96, 128,
            zlib.crc32(multi_texture[96:224]) & 0xFFFFFFFF,
        )
        assert multi_texture[96:100] == struct.pack("<HH", 0x07E0, 0xF800)

        result = invoke(
            converter, "--emit-asset", "--section-directory",
            "--lz4-vertices", multi_gltf_source, multi_gltf_asset,
        )
        assert result.returncode == 0, result.stderr
        multi_lz4 = multi_gltf_asset.read_bytes()
        multi_lz4_descriptors = [
            struct.unpack_from("<7IHH", multi_lz4, 64 + i * 32)
            for i in range(15)
        ]
        assert multi_lz4_descriptors[0][7] == 1
        assert multi_lz4_descriptors[6][7] == 1

        result = invoke(converter, "--emit-asset", gltf_source, gltf_asset)
        assert result.returncode == 2
        assert result.stderr == (
            "glTF input requires --emit-asset --section-directory\n"
        )

        skin_joints = bytes((0, 0, 0, 0,
                             1, 0, 0, 0,
                             0, 1, 0, 0))
        skin_weights = struct.pack(
            "<12f",
            1.0, 0.0, 0.0, 0.0,
            1.0, 0.0, 0.0, 0.0,
            0.25, 0.75, 0.0, 0.0,
        )
        identity_matrix = (
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0,
        )
        gltf_skin_binary = (
            gltf_binary[:72] + skin_joints + skin_weights +
            struct.pack("<3H", 0, 1, 2) + b"\0\0" +
            struct.pack("<32f", *(identity_matrix * 2))
        )
        gltf_skin_source = root / "triangle-skin.gltf"
        gltf_skin_source.write_text(json.dumps({
            "asset": {"version": "2.0"},
            "buffers": [{
                "byteLength": len(gltf_skin_binary),
                "uri": "data:application/octet-stream;base64," +
                       base64.b64encode(gltf_skin_binary).decode("ascii"),
            }],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 36, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 72, "byteLength": 12,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 84, "byteLength": 48,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 132, "byteLength": 6,
                 "target": 34963},
                {"buffer": 0, "byteOffset": 140, "byteLength": 128},
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 1, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 2, "componentType": 5121, "count": 3,
                 "type": "VEC4"},
                {"bufferView": 3, "componentType": 5126, "count": 3,
                 "type": "VEC4"},
                {"bufferView": 4, "componentType": 5123, "count": 3,
                 "type": "SCALAR"},
                {"bufferView": 5, "componentType": 5126, "count": 2,
                 "type": "MAT4"},
            ],
            "meshes": [{"primitives": [{
                "attributes": {
                    "POSITION": 0,
                    "NORMAL": 1,
                    "JOINTS_0": 2,
                    "WEIGHTS_0": 3,
                },
                "indices": 4,
            }]}],
            "skins": [{"joints": [1, 2], "inverseBindMatrices": 5}],
            "nodes": [
                {"name": "root", "children": [1, 3]},
                {"name": "joint-0", "children": [2]},
                {"name": "joint-1"},
                {"name": "mesh", "mesh": 0, "skin": 0},
            ],
            "scenes": [{"nodes": [0]}],
            "scene": 0,
        }), encoding="utf-8")
        gltf_skin_asset = root / "triangle-gltf-skin.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            gltf_skin_source, gltf_skin_asset,
        )
        assert result.returncode == 0, result.stderr
        assert "hierarchy_nodes=4\n" in result.stdout
        assert "general_skin_spans=3\n" in result.stdout
        assert "general_skin_weights=4\n" in result.stdout
        assert "skeleton_joints=2\n" in result.stdout
        gltf_skin_asset_bytes = gltf_skin_asset.read_bytes()
        assert struct.unpack_from("<I", gltf_skin_asset_bytes, 32)[0] == 6
        gltf_skin_descriptor = struct.unpack_from(
            "<7IHH", gltf_skin_asset_bytes, 64 + 3 * 32
        )
        assert gltf_skin_descriptor[0] == 6
        gltf_skin_offset = gltf_skin_descriptor[2]
        gltf_skin_size = gltf_skin_descriptor[3]
        gltf_skin_section = gltf_skin_asset_bytes[
            gltf_skin_offset:gltf_skin_offset + gltf_skin_size
        ]
        assert struct.unpack_from("<III", gltf_skin_section, 12) == (
            3, 4, 2
        )
        assert tuple(
            struct.unpack_from("<HH", gltf_skin_section, 72 + index * 4)
            for index in range(4)
        ) == ((0, 65535), (1, 65535), (0, 16384), (1, 49151))
        gltf_skeleton_descriptor = struct.unpack_from(
            "<7IHH", gltf_skin_asset_bytes, 64 + 4 * 32
        )
        assert gltf_skeleton_descriptor[0] == 11
        gltf_skeleton_offset = gltf_skeleton_descriptor[2]
        gltf_skeleton_size = gltf_skeleton_descriptor[3]
        gltf_skeleton = gltf_skin_asset_bytes[
            gltf_skeleton_offset:gltf_skeleton_offset + gltf_skeleton_size
        ]
        assert struct.unpack_from("<II", gltf_skeleton, 12) == (2, 4)
        assert struct.unpack_from("<I", gltf_skeleton, 48)[0] == 1
        assert struct.unpack_from("<I", gltf_skeleton, 128)[0] == 2

        many_joints_0 = bytes((
            0, 1, 2, 3,
            0, 0, 0, 0,
            0, 1, 0, 0,
        ))
        many_weights_0 = struct.pack(
            "<12f",
            0.2, 0.2, 0.2, 0.2,
            1.0, 0.0, 0.0, 0.0,
            0.25, 0.75, 0.0, 0.0,
        )
        many_joints_1 = bytes((
            4, 5, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
        ))
        many_weights_1 = struct.pack(
            "<12f",
            0.1, 0.1, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0,
        )
        many_skin_binary = (
            gltf_binary[:72] + many_joints_0 + many_weights_0 +
            many_joints_1 + many_weights_1 +
            struct.pack("<3H", 0, 1, 2) + b"\0\0" +
            struct.pack("<96f", *(identity_matrix * 6))
        )
        many_skin_document = {
            "asset": {"version": "2.0"},
            "buffers": [{
                "byteLength": len(many_skin_binary),
                "uri": "data:application/octet-stream;base64," +
                       base64.b64encode(many_skin_binary).decode("ascii"),
            }],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 36, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 72, "byteLength": 12,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 84, "byteLength": 48,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 132, "byteLength": 12,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 144, "byteLength": 48,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 192, "byteLength": 6,
                 "target": 34963},
                {"buffer": 0, "byteOffset": 200, "byteLength": 384},
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 1, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 2, "componentType": 5121, "count": 3,
                 "type": "VEC4"},
                {"bufferView": 3, "componentType": 5126, "count": 3,
                 "type": "VEC4"},
                {"bufferView": 4, "componentType": 5121, "count": 3,
                 "type": "VEC4"},
                {"bufferView": 5, "componentType": 5126, "count": 3,
                 "type": "VEC4"},
                {"bufferView": 6, "componentType": 5123, "count": 3,
                 "type": "SCALAR"},
                {"bufferView": 7, "componentType": 5126, "count": 6,
                 "type": "MAT4"},
            ],
            "meshes": [{"primitives": [{
                "attributes": {
                    "POSITION": 0, "NORMAL": 1,
                    "JOINTS_0": 2, "WEIGHTS_0": 3,
                    "JOINTS_1": 4, "WEIGHTS_1": 5,
                },
                "indices": 6,
            }]}],
            "skins": [{
                "joints": [1, 2, 3, 4, 5, 6],
                "inverseBindMatrices": 7,
            }],
            "nodes": [
                {"children": [1, 2, 3, 4, 5, 6, 7]},
                {}, {}, {}, {}, {}, {},
                {"mesh": 0, "skin": 0},
            ],
            "scenes": [{"nodes": [0]}],
            "scene": 0,
        }
        many_skin_source = root / "many-influence-skin.gltf"
        many_skin_source.write_text(
            json.dumps(many_skin_document), encoding="utf-8"
        )
        many_skin_asset = root / "many-influence-skin.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            many_skin_source, many_skin_asset,
        )
        assert result.returncode == 0, result.stderr
        assert "hierarchy_nodes=8\n" in result.stdout
        assert "general_skin_spans=3\n" in result.stdout
        assert "general_skin_weights=9\n" in result.stdout
        assert "skeleton_joints=6\n" in result.stdout
        many_skin_bytes = many_skin_asset.read_bytes()
        many_skin_descriptor = struct.unpack_from(
            "<7IHH", many_skin_bytes, 64 + 3 * 32
        )
        many_skin_section = many_skin_bytes[
            many_skin_descriptor[2]:
            many_skin_descriptor[2] + many_skin_descriptor[3]
        ]
        assert struct.unpack_from("<III", many_skin_section, 12) == (
            3, 9, 6
        )
        first_vertex_weights = [
            struct.unpack_from("<HH", many_skin_section, 72 + index * 4)
            for index in range(6)
        ]
        assert [joint for joint, _ in first_vertex_weights] == list(range(6))
        assert sum(weight for _, weight in first_vertex_weights) == 65535

        unbound_document = json.loads(json.dumps(many_skin_document))
        del unbound_document["skins"]
        del unbound_document["nodes"][7]["skin"]
        unbound_source = root / "unbound-skin-attributes.gltf"
        unbound_source.write_text(
            json.dumps(unbound_document), encoding="utf-8"
        )
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            unbound_source, root / "unbound-skin-attributes.pcm",
        )
        assert result.returncode != 0

        morph_binary = (
            gltf_binary +
            struct.pack(
                "<9f",
                0.0, 0.0, 0.0,
                0.5, 0.0, 0.0,
                0.0, 0.0, 0.0,
            ) +
            struct.pack("<2f", 0.0, 1.0) +
            struct.pack("<2f", 0.0, 1.0)
        )
        gltf_morph_source = root / "triangle-morph.gltf"
        gltf_morph_source.write_text(json.dumps({
            "asset": {"version": "2.0"},
            "buffers": [{
                "byteLength": len(morph_binary),
                "uri": "data:application/octet-stream;base64," +
                       base64.b64encode(morph_binary).decode("ascii"),
            }],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 36, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 72, "byteLength": 6,
                 "target": 34963},
                {"buffer": 0, "byteOffset": 80, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 116, "byteLength": 8},
                {"buffer": 0, "byteOffset": 124, "byteLength": 8},
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 1, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 2, "componentType": 5123, "count": 3,
                 "type": "SCALAR"},
                {"bufferView": 3, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 4, "componentType": 5126, "count": 2,
                 "type": "SCALAR"},
                {"bufferView": 5, "componentType": 5126, "count": 2,
                 "type": "SCALAR"},
            ],
            "meshes": [{"primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1},
                "indices": 2,
                "targets": [{"POSITION": 3}],
            }], "weights": [0.25]}],
            "nodes": [{"mesh": 0}],
            "animations": [{
                "samplers": [{"input": 4, "output": 5,
                              "interpolation": "LINEAR"}],
                "channels": [{"sampler": 0,
                              "target": {"node": 0,
                                         "path": "weights"}}],
            }],
            "scenes": [{"nodes": [0]}],
            "scene": 0,
        }), encoding="utf-8")
        gltf_morph_asset = root / "triangle-gltf-morph.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            gltf_morph_source, gltf_morph_asset,
        )
        assert result.returncode == 0, result.stderr
        assert "morph_targets=1\n" in result.stdout
        assert "morph_deltas=1\n" in result.stdout
        assert "morph_animation_bindings=1\n" in result.stdout
        assert "morph_animation_tracks=1\n" in result.stdout
        assert "morph_animation_keys=2\n" in result.stdout
        gltf_morph_asset_bytes = gltf_morph_asset.read_bytes()
        assert struct.unpack_from("<I", gltf_morph_asset_bytes, 32)[0] == 6
        gltf_morph_descriptor = struct.unpack_from(
            "<7IHH", gltf_morph_asset_bytes, 64 + 3 * 32
        )
        assert gltf_morph_descriptor[0] == 7
        gltf_morph_offset = gltf_morph_descriptor[2]
        gltf_morph_size = gltf_morph_descriptor[3]
        gltf_morph = gltf_morph_asset_bytes[
            gltf_morph_offset:gltf_morph_offset + gltf_morph_size
        ]
        assert struct.unpack_from("<II", gltf_morph, 12) == (1, 1)
        assert struct.unpack_from("<HH6f", gltf_morph, 56) == (
            1, 0, 0.5, 0.0, 0.0, 0.0, 0.0, 0.0
        )
        gltf_morph_animation_descriptor = struct.unpack_from(
            "<7IHH", gltf_morph_asset_bytes, 64 + 4 * 32
        )
        assert gltf_morph_animation_descriptor[0] == 13
        gltf_morph_animation_offset = gltf_morph_animation_descriptor[2]
        gltf_morph_animation_size = gltf_morph_animation_descriptor[3]
        gltf_morph_animation = gltf_morph_asset_bytes[
            gltf_morph_animation_offset:
            gltf_morph_animation_offset + gltf_morph_animation_size
        ]
        assert gltf_morph_animation[:4] == b"PMW1"
        assert struct.unpack_from("<H", gltf_morph_animation, 4) == (2,)
        assert struct.unpack_from("<4I", gltf_morph_animation, 12) == (
            1, 1, 1, 2
        )
        assert struct.unpack_from("<ff", gltf_morph_animation, 28) == (
            0.0, 1.0
        )
        assert struct.unpack_from("<4I", gltf_morph_animation, 64) == (
            0, 0, 0, 1
        )
        assert struct.unpack_from("<If", gltf_morph_animation, 80) == (
            0, 0.25
        )
        assert struct.unpack_from("<HHIII", gltf_morph_animation, 88) == (
            1, 0, 0, 2, 0
        )
        assert struct.unpack_from("<4f", gltf_morph_animation, 104) == (
            0.0, 0.0, 0.0, 0.0
        )
        assert struct.unpack_from("<4f", gltf_morph_animation, 120) == (
            1.0, 1.0, 0.0, 0.0
        )

        cubic_morph_binary = morph_binary[:124] + struct.pack(
            "<6f", 0.0, 0.0, 2.0, 0.0, 4.0, 0.0
        )
        cubic_morph_document = json.loads(
            gltf_morph_source.read_text(encoding="utf-8")
        )
        cubic_morph_document["buffers"][0] = {
            "byteLength": len(cubic_morph_binary),
            "uri": "data:application/octet-stream;base64," +
                   base64.b64encode(cubic_morph_binary).decode("ascii"),
        }
        cubic_morph_document["bufferViews"][5]["byteLength"] = 24
        cubic_morph_document["accessors"][5]["count"] = 6
        cubic_morph_document["animations"][0]["samplers"][0][
            "interpolation"
        ] = "CUBICSPLINE"
        cubic_morph_source = root / "triangle-cubic-morph.gltf"
        cubic_morph_source.write_text(
            json.dumps(cubic_morph_document), encoding="utf-8"
        )
        cubic_morph_asset = root / "triangle-cubic-morph.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            cubic_morph_source, cubic_morph_asset,
        )
        assert result.returncode == 0, result.stderr
        cubic_morph_bytes = cubic_morph_asset.read_bytes()
        cubic_morph_descriptor = struct.unpack_from(
            "<7IHH", cubic_morph_bytes, 64 + 4 * 32
        )
        cubic_morph_animation = cubic_morph_bytes[
            cubic_morph_descriptor[2]:
            cubic_morph_descriptor[2] + cubic_morph_descriptor[3]
        ]
        assert struct.unpack_from("<H", cubic_morph_animation, 88) == (3,)
        assert struct.unpack_from("<4f", cubic_morph_animation, 104) == (
            0.0, 0.0, 0.0, 2.0
        )
        assert struct.unpack_from("<4f", cubic_morph_animation, 120) == (
            1.0, 4.0, 0.0, 0.0
        )

        animation_binary = (
            gltf_binary + struct.pack("<2f", 0.0, 2.0) +
            struct.pack("<6f", 0.0, 3.0, 0.0, 0.0, 5.0, 0.0)
        )
        gltf_animation_source = root / "triangle-animation.gltf"
        gltf_animation_source.write_text(json.dumps({
            "asset": {"version": "2.0"},
            "buffers": [{
                "byteLength": len(animation_binary),
                "uri": "data:application/octet-stream;base64," +
                       base64.b64encode(animation_binary).decode("ascii"),
            }],
            "bufferViews": [
                {"buffer": 0, "byteOffset": 0, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 36, "byteLength": 36,
                 "target": 34962},
                {"buffer": 0, "byteOffset": 72, "byteLength": 6,
                 "target": 34963},
                {"buffer": 0, "byteOffset": 80, "byteLength": 8},
                {"buffer": 0, "byteOffset": 88, "byteLength": 24},
            ],
            "accessors": [
                {"bufferView": 0, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 1, "componentType": 5126, "count": 3,
                 "type": "VEC3"},
                {"bufferView": 2, "componentType": 5123, "count": 3,
                 "type": "SCALAR"},
                {"bufferView": 3, "componentType": 5126, "count": 2,
                 "type": "SCALAR"},
                {"bufferView": 4, "componentType": 5126, "count": 2,
                 "type": "VEC3"},
            ],
            "meshes": [{"primitives": [{
                "attributes": {"POSITION": 0, "NORMAL": 1},
                "indices": 2,
            }]}],
            "nodes": [
                {"translation": [2.0, 0.0, 0.0], "children": [1]},
                {"mesh": 0, "translation": [0.0, 3.0, 0.0]},
            ],
            "animations": [{
                "samplers": [{"input": 3, "output": 4,
                              "interpolation": "LINEAR"}],
                "channels": [{"sampler": 0,
                              "target": {"node": 1,
                                         "path": "translation"}}],
            }],
            "scenes": [{"nodes": [0]}],
            "scene": 0,
        }), encoding="utf-8")
        gltf_animation_asset = root / "triangle-gltf-animation.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            gltf_animation_source, gltf_animation_asset,
        )
        assert result.returncode == 0, result.stderr
        assert "animation_transforms=2\n" in result.stdout
        assert "animation_tracks=1\n" in result.stdout
        assert "animation_keys=2\n" in result.stdout
        gltf_animation_asset_bytes = gltf_animation_asset.read_bytes()
        assert struct.unpack_from("<I", gltf_animation_asset_bytes, 32)[0] == 5
        gltf_animation_descriptor = struct.unpack_from(
            "<7IHH", gltf_animation_asset_bytes, 64 + 3 * 32
        )
        assert gltf_animation_descriptor[0] == 9
        gltf_animation_offset = gltf_animation_descriptor[2]
        gltf_animation_size = gltf_animation_descriptor[3]
        gltf_animation = gltf_animation_asset_bytes[
            gltf_animation_offset:
            gltf_animation_offset + gltf_animation_size
        ]
        assert struct.unpack_from("<III", gltf_animation, 12) == (2, 1, 2)
        assert struct.unpack_from("<ff", gltf_animation, 44) == (0.0, 2.0)
        assert struct.unpack_from("<4I", gltf_animation, 128) == (
            0, 0xffffffff, 0xffffffff, 0xffffffff
        )
        assert struct.unpack_from("<5fI", gltf_animation, 208) == (
            0.0, 0.0, 3.0, 0.0, 1.0, 0
        )
        assert struct.unpack_from("<5fI", gltf_animation, 264) == (
            2.0, 0.0, 5.0, 0.0, 1.0, 0
        )

        cubic_animation_binary = (
            gltf_binary + struct.pack("<2f", 0.0, 2.0) +
            struct.pack(
                "<18f",
                0.0, 0.0, 0.0,
                0.0, 0.0, 0.0,
                2.0, 0.0, 0.0,
                0.0, 0.0, 0.0,
                4.0, 0.0, 0.0,
                0.0, 0.0, 0.0,
            )
        )
        cubic_animation_document = json.loads(
            gltf_animation_source.read_text(encoding="utf-8")
        )
        cubic_animation_document["buffers"][0] = {
            "byteLength": len(cubic_animation_binary),
            "uri": "data:application/octet-stream;base64," +
                   base64.b64encode(cubic_animation_binary).decode("ascii"),
        }
        cubic_animation_document["bufferViews"][4]["byteLength"] = 72
        cubic_animation_document["accessors"][4]["count"] = 6
        cubic_animation_document["animations"][0]["samplers"][0][
            "interpolation"
        ] = "CUBICSPLINE"
        cubic_animation_source = root / "triangle-cubic-animation.gltf"
        cubic_animation_source.write_text(
            json.dumps(cubic_animation_document), encoding="utf-8"
        )
        cubic_animation_asset = root / "triangle-cubic-animation.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            cubic_animation_source, cubic_animation_asset,
        )
        assert result.returncode == 0, result.stderr
        cubic_asset_bytes = cubic_animation_asset.read_bytes()
        cubic_descriptor = struct.unpack_from(
            "<7IHH", cubic_asset_bytes, 64 + 3 * 32
        )
        cubic_animation = cubic_asset_bytes[
            cubic_descriptor[2]:cubic_descriptor[2] + cubic_descriptor[3]
        ]
        assert struct.unpack_from("<HH", cubic_animation, 192) == (1, 3)
        assert struct.unpack_from("<4f", cubic_animation, 208) == (
            0.0, 0.0, 0.0, 0.0
        )
        assert struct.unpack_from("<4f", cubic_animation, 244) == (
            2.0, 0.0, 0.0, 0.0
        )
        assert struct.unpack_from("<4f", cubic_animation, 264) == (
            2.0, 4.0, 0.0, 0.0
        )

        cubic_rotation_binary = (
            gltf_binary + struct.pack("<2f", 0.0, 2.0) +
            struct.pack(
                "<24f",
                0.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 1.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                1.0, 0.0, 0.0, 0.0,
                0.0, 0.0, 0.0, 0.0,
            )
        )
        cubic_rotation_document = json.loads(
            gltf_animation_source.read_text(encoding="utf-8")
        )
        cubic_rotation_document["buffers"][0] = {
            "byteLength": len(cubic_rotation_binary),
            "uri": "data:application/octet-stream;base64," +
                   base64.b64encode(cubic_rotation_binary).decode("ascii"),
        }
        cubic_rotation_document["bufferViews"][4]["byteLength"] = 96
        cubic_rotation_document["accessors"][4]["count"] = 6
        cubic_rotation_document["accessors"][4]["type"] = "VEC4"
        cubic_rotation_document["animations"][0]["samplers"][0][
            "interpolation"
        ] = "CUBICSPLINE"
        cubic_rotation_document["animations"][0]["channels"][0][
            "target"
        ]["path"] = "rotation"
        cubic_rotation_source = root / "triangle-cubic-rotation.gltf"
        cubic_rotation_source.write_text(
            json.dumps(cubic_rotation_document), encoding="utf-8"
        )
        cubic_rotation_asset = root / "triangle-cubic-rotation.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            cubic_rotation_source, cubic_rotation_asset,
        )
        assert result.returncode == 0, result.stderr
        cubic_rotation_bytes = cubic_rotation_asset.read_bytes()
        cubic_rotation_descriptor = struct.unpack_from(
            "<7IHH", cubic_rotation_bytes, 64 + 3 * 32
        )
        cubic_rotation = cubic_rotation_bytes[
            cubic_rotation_descriptor[2]:
            cubic_rotation_descriptor[2] + cubic_rotation_descriptor[3]
        ]
        assert struct.unpack_from("<HH", cubic_rotation, 192) == (2, 3)
        assert struct.unpack_from("<4f", cubic_rotation, 212) == (
            1.0, 0.0, 0.0, 0.0
        )
        assert struct.unpack_from("<4f", cubic_rotation, 244) == (
            0.0, 0.0, 1.0, 0.0
        )
        assert struct.unpack_from("<4f", cubic_rotation, 268) == (
            0.0, 1.0, 0.0, 0.0
        )
        assert struct.unpack_from("<4f", cubic_rotation, 284) == (
            0.0, 0.0, 0.0, 1.0
        )

        multi_animation_binary = morph_binary + struct.pack(
            "<6f", 0.0, 0.0, 0.0, 2.0, 0.0, 0.0
        )
        multi_animation_document = json.loads(
            gltf_morph_source.read_text(encoding="utf-8")
        )
        multi_animation_document["buffers"][0] = {
            "byteLength": len(multi_animation_binary),
            "uri": "data:application/octet-stream;base64," +
                   base64.b64encode(multi_animation_binary).decode("ascii"),
        }
        multi_animation_document["bufferViews"].append({
            "buffer": 0, "byteOffset": 132, "byteLength": 24,
        })
        multi_animation_document["accessors"].append({
            "bufferView": 6, "componentType": 5126, "count": 2,
            "type": "VEC3",
        })
        multi_animation_document["animations"] = [
            {
                "name": "move",
                "samplers": [{"input": 4, "output": 6,
                              "interpolation": "LINEAR"}],
                "channels": [{"sampler": 0,
                              "target": {"node": 0,
                                         "path": "translation"}}],
            },
            {
                "samplers": [{"input": 4, "output": 5,
                              "interpolation": "LINEAR"}],
                "channels": [{"sampler": 0,
                              "target": {"node": 0,
                                         "path": "weights"}}],
            },
            {
                "name": "combo",
                "samplers": [
                    {"input": 4, "output": 6,
                     "interpolation": "LINEAR"},
                    {"input": 4, "output": 5,
                     "interpolation": "LINEAR"},
                ],
                "channels": [
                    {"sampler": 0,
                     "target": {"node": 0, "path": "translation"}},
                    {"sampler": 1,
                     "target": {"node": 0, "path": "weights"}},
                ],
            },
        ]
        multi_animation_source = root / "multi-animation.gltf"
        multi_animation_source.write_text(
            json.dumps(multi_animation_document), encoding="utf-8"
        )
        multi_animation_asset = root / "multi-animation.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            multi_animation_source, multi_animation_asset,
        )
        assert result.returncode == 0, result.stderr
        assert "animation_clips=3\n" in result.stdout
        assert "animation_transforms=2\n" in result.stdout
        assert "animation_tracks=2\n" in result.stdout
        assert "animation_keys=4\n" in result.stdout
        assert "morph_animation_bindings=2\n" in result.stdout
        assert "morph_animation_tracks=2\n" in result.stdout
        assert "morph_animation_keys=4\n" in result.stdout
        multi_animation_bytes = multi_animation_asset.read_bytes()
        assert struct.unpack_from("<I", multi_animation_bytes, 32)[0] == 10
        section_types = [
            struct.unpack_from(
                "<I", multi_animation_bytes, 64 + index * 32
            )[0]
            for index in range(10)
        ]
        assert section_types == [
            1, 2, 7, 8, 9, 13, 9, 13, 14, 12
        ]
        catalog_descriptor = struct.unpack_from(
            "<7IHH", multi_animation_bytes, 64 + 8 * 32
        )
        catalog = multi_animation_bytes[
            catalog_descriptor[2]:
            catalog_descriptor[2] + catalog_descriptor[3]
        ]
        assert catalog[:4] == b"PAC1"
        assert struct.unpack_from("<II", catalog, 8) == (169, 3)
        assert struct.unpack_from("<H", catalog, 16)[0] == 32
        assert struct.unpack_from("<II", catalog, 20) == (96, 9)
        assert struct.unpack_from("<4I2f2I", catalog, 64) == (
            0, 0xffffffff, 0, 4, 0.0, 1.0, 0, 0
        )
        assert struct.unpack_from("<4I2f2I", catalog, 96) == (
            0xffffffff, 0, 4, 0, 0.0, 1.0, 0, 0
        )
        assert struct.unpack_from("<4I2f2I", catalog, 128) == (
            1, 1, 4, 5, 0.0, 1.0, 0, 0
        )
        assert catalog[160:] == b"movecombo"

        duplicate_name_document = json.loads(
            json.dumps(multi_animation_document)
        )
        duplicate_name_document["animations"][2]["name"] = "move"
        duplicate_name_source = root / "duplicate-animation-names.gltf"
        duplicate_name_source.write_text(
            json.dumps(duplicate_name_document), encoding="utf-8"
        )
        duplicate_name_asset = root / "duplicate-animation-names.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            duplicate_name_source, duplicate_name_asset,
        )
        assert result.returncode == 0, result.stderr
        duplicate_name_bytes = duplicate_name_asset.read_bytes()
        duplicate_catalog_descriptor = struct.unpack_from(
            "<7IHH", duplicate_name_bytes, 64 + 8 * 32
        )
        duplicate_catalog = duplicate_name_bytes[
            duplicate_catalog_descriptor[2]:
            duplicate_catalog_descriptor[2] +
            duplicate_catalog_descriptor[3]
        ]
        assert struct.unpack_from("<II", duplicate_catalog, 8) == (160, 3)
        assert struct.unpack_from("<I", duplicate_catalog, 24)[0] == 0
        assert [
            struct.unpack_from("<II", duplicate_catalog,
                               64 + index * 32 + 8)
            for index in range(3)
        ] == [(0, 0), (0, 0), (0, 0)]

        matrix_animation_document = json.loads(
            gltf_animation_source.read_text(encoding="utf-8")
        )
        matrix_animation_document["nodes"][0] = {
            "matrix": [
                2.0, 0.0, 0.0, 0.0,
                0.0, 3.0, 0.0, 0.0,
                0.0, 0.0, -4.0, 0.0,
                2.0, 4.0, 6.0, 1.0,
            ],
            "children": [1],
        }
        matrix_animation_source = root / "matrix-animation.gltf"
        matrix_animation_source.write_text(
            json.dumps(matrix_animation_document), encoding="utf-8"
        )
        matrix_animation_asset = root / "matrix-animation.pcm"
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            matrix_animation_source, matrix_animation_asset,
        )
        assert result.returncode == 0, result.stderr
        matrix_animation_bytes = matrix_animation_asset.read_bytes()
        matrix_animation_descriptor = struct.unpack_from(
            "<7IHH", matrix_animation_bytes, 64 + 3 * 32
        )
        matrix_animation = matrix_animation_bytes[
            matrix_animation_descriptor[2]:
            matrix_animation_descriptor[2] + matrix_animation_descriptor[3]
        ]
        matrix_fallback = struct.unpack_from(
            "<4I3f4f3f2I", matrix_animation, 64
        )
        assert matrix_fallback[:4] == (0xffffffff,) * 4
        assert matrix_fallback[4:7] == (2.0, 4.0, 6.0)
        assert math.isclose(matrix_fallback[7], 0.0, abs_tol=1e-6)
        assert math.isclose(matrix_fallback[8], 0.0, abs_tol=1e-6)
        assert math.isclose(abs(matrix_fallback[9]), 1.0, abs_tol=1e-6)
        assert math.isclose(matrix_fallback[10], 0.0, abs_tol=1e-6)
        assert matrix_fallback[11:14] == (-2.0, 3.0, 4.0)
        assert matrix_fallback[14:] == (1, 0)

        sheared_animation_document = json.loads(
            json.dumps(matrix_animation_document)
        )
        sheared_animation_document["nodes"][0]["matrix"][4] = 0.25
        sheared_animation_source = root / "sheared-animation.gltf"
        sheared_animation_source.write_text(
            json.dumps(sheared_animation_document), encoding="utf-8"
        )
        result = invoke(
            converter, "--emit-asset", "--section-directory",
            sheared_animation_source, root / "sheared-animation.pcm",
        )
        assert result.returncode != 0

        directory_lz4 = root / "triangle-directory-lz4.pcm"
        result = invoke(
            converter,
            "--texture-id", "7",
            "--emit-asset",
            "--section-directory",
            "--lz4-vertices",
            source,
            directory_lz4,
        )
        assert result.returncode == 0, result.stderr
        directory_lz4_bytes = directory_lz4.read_bytes()
        lz4_descriptor = struct.unpack_from(
            "<7IHH", directory_lz4_bytes, 64
        )
        assert lz4_descriptor[7:] == (1, 4)
        assert directory_lz4_bytes[
            lz4_descriptor[2]:lz4_descriptor[2] + 4
        ] == b"\x04\x22\x4d\x18"

        lz4_asset = root / "triangle-lz4.pcm"
        result = invoke(
            converter,
            "--texture-id", "7",
            "--emit-asset",
            "--lz4-vertices",
            source,
            lz4_asset,
        )
        assert result.returncode == 0, result.stderr
        lz4_bytes = lz4_asset.read_bytes()
        assert result.stdout == (
            REPORT +
            f"asset_bytes={len(lz4_bytes)}\nvertex_codec=lz4-frame\n"
        )
        assert struct.unpack_from("<H", lz4_bytes, 48)[0] == 1
        lz4_vertex_offset = struct.unpack_from("<I", lz4_bytes, 32)[0]
        assert lz4_bytes[lz4_vertex_offset:lz4_vertex_offset + 4] == (
            b"\x04\x22\x4d\x18"
        )
        lz4_again = root / "triangle-lz4-again.pcm"
        result = invoke(
            converter,
            "--texture-id", "7",
            "--emit-asset",
            "--lz4-vertices",
            source,
            lz4_again,
        )
        assert result.returncode == 0, result.stderr
        assert lz4_again.read_bytes() == lz4_bytes

        result = invoke(
            converter, "--lz4-vertices", source, vertices, polygons
        )
        assert result.returncode == 2
        assert result.stderr == "--lz4-vertices requires --emit-asset\n"

        result = invoke(
            converter, "--emit-asset", "--cooked-cache", source, raw_asset
        )
        assert result.returncode == 2
        assert result.stderr == (
            "--cooked-cache requires --emit-asset --section-directory\n"
        )

        result = invoke(
            converter, "--emit-asset", "--scene-root", source, raw_asset
        )
        assert result.returncode == 2
        assert result.stderr == (
            "--scene-root requires --emit-asset --section-directory\n"
        )

        result = invoke(
            converter, "--emit-asset", "--rigid-skin", source, raw_asset
        )
        assert result.returncode == 2
        assert result.stderr == (
            "--rigid-skin requires --emit-asset --section-directory\n"
        )

        result = invoke(
            converter, "--emit-asset", "--morph-target", "1", "0", "0",
            source, raw_asset
        )
        assert result.returncode == 2
        assert result.stderr == (
            "--morph-target requires --emit-asset --section-directory\n"
        )

        result = invoke(
            converter, "--emit-asset", "--section-directory",
            "--animation-offset", "1", "0", "0", source, raw_asset
        )
        assert result.returncode == 2
        assert result.stderr == (
            "--animation-offset requires --emit-asset "
            "--section-directory --scene-root\n"
        )

        generated_again = root / "test-model-again.c"
        result = invoke(
            converter,
            "--texture-id", "7",
            "--emit-c", "test_model",
            source,
            generated_again,
        )
        assert result.returncode == 0, result.stderr
        assert generated_again.read_bytes() == generated.read_bytes()

        generated.write_bytes(b"generated sentinel")
        for invalid_symbol in (
            "9model", "_model", "static", "model-name", "m" * 32
        ):
            result = invoke(
                converter,
                "--texture-id", "7",
                "--emit-c", invalid_symbol,
                source,
                generated,
            )
            assert result.returncode == 2, invalid_symbol
            assert result.stdout == ""
            assert result.stderr.startswith("usage: ")
            assert generated.read_bytes() == b"generated sentinel"

        result = invoke(
            converter,
            "--texture-id", "7",
            "--emit-c", "test_model",
            source,
            source,
        )
        assert result.returncode == 2
        assert result.stdout == ""
        assert result.stderr == "input and output paths must be distinct\n"

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
            17 | (8 << 8), 2, 0xFFFF, 0xFFFF,
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
            17 | (8 << 8), 2, 0xFFFF, 0xFFFF,
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
        expected[10:16] = [0, 0, 1024, 0, 0, 0x7FFF]
        expected[16:22] = [2, 0, 0, 0, 0, 0x7FFF]
        expected[22:28] = [1, 1024, 1024, 0, 0, 0x7FFF]
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
            "<12H", 17 | (8 << 8), 2, 0xFFFF, 0xFFFF,
            64, 5, 1, 3, 0, 1, 2, 0xFF
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
        assert "maximum_strip_vertices=10922\n" in result.stdout
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
            17 | (8 << 8), 2, 0xFFFF, 0xFFFF, 8, 2, 77, 21, 2
        )
        assert material_words[29:32] == (8, 9, 77)
        assert material_words[44:47] == (8, 2, 77)
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
d 0.5
newmtl green
Kd 0 1 0
Tr 0
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
            23 | (0x25 << 8), 6,
            0x8000, 0x80FF,
            0x2000, 0xFF40,
            0x334D, 0x081A,
            64 | (8 << 8), 5, 1, 3, 0, 1, 2,
            17 | (8 << 8), 2, 0xFF00, 0xFF00,
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
