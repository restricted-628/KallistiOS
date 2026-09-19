"""Independent byte-level tests of the opt-in emissive compiler profile.

Copyright (C) 2026 Joseph Black
"""
import base64
import copy
import json
import math
import pathlib
import struct
import sys
import zlib

sys.dont_write_bytecode = True


def sections(blob):
    assert blob[:4] == b"PCM2"
    assert struct.unpack_from("<I", blob, 8)[0] == len(blob)
    assert zlib.crc32(blob[:60]) == struct.unpack_from("<I", blob, 60)[0]
    count, offset, size = struct.unpack_from("<3I", blob, 32)
    assert size == count * 32
    assert zlib.crc32(blob[offset:offset + size]) == struct.unpack_from(
        "<I", blob, 44)[0]
    result = {}
    for i in range(count):
        kind, flags, start, stored, decoded, crc, reserved, codec, align = \
            struct.unpack_from("<7IHH", blob, offset + i * 32)
        assert start % align == 0 and reserved == 0
        assert flags == (1 if kind in (16, 17) else 0)
        data = blob[start:start + stored]
        if codec == 0:
            assert stored == decoded and zlib.crc32(data) == crc
        result.setdefault(kind, []).append(data)
    return result


def fixture(png_rgba):
    binary = (struct.pack("<12f", -1, -1, 1, 1, -1, 1,
                          -1, 1, 1, 1, 1, 1) +
              struct.pack("<8f", 0, 0, 1, 0, 0, 1, 1, 1) +
              struct.pack("<8f", -1, 0, 1, 0, -1, 1, 1, 1) +
              struct.pack("<4H", 0, 1, 2, 3))
    png = png_rgba(8, 8, bytes([128, 64, 255, 0]) * 64)
    return {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary), "uri":
                     "data:application/octet-stream;base64," +
                     base64.b64encode(binary).decode()}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 48},
            {"buffer": 0, "byteOffset": 48, "byteLength": 32},
            {"buffer": 0, "byteOffset": 80, "byteLength": 32},
            {"buffer": 0, "byteOffset": 112, "byteLength": 8}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 4, "type": "SCALAR"}],
        "images": [{"uri": "data:image/png;base64," + base64.b64encode(png).decode()}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728,
                      "wrapS": 33071, "wrapT": 33648}],
        "textures": [{"source": 0, "sampler": 0}],
        "materials": [{
            "pbrMetallicRoughness": {"baseColorTexture": {
                "index": 0, "extensions": {"KHR_texture_transform": {
                    "scale": [0, 0]}}}},
            "emissiveFactor": [0.25, 0.5, 0],
            "emissiveTexture": {"index": 0, "texCoord": 1, "extensions": {
                "KHR_texture_transform": {"offset": [0.25, -1],
                                          "scale": [-2, 0.5]}}}}],
        "meshes": [{"primitives": [{"attributes": {
            "POSITION": 0, "TEXCOORD_0": 1, "TEXCOORD_1": 2},
            "indices": 3, "material": 0, "mode": 5}]}],
        "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0,
        "extensionsUsed": ["KHR_texture_transform"]}


def run(converter, root, invoke, png_rgba):
    root = root / "emissive"
    root.mkdir(exist_ok=True)
    document = fixture(png_rgba)

    def convert(doc, name, flags=(), good=True, profile=True):
        source, output = root / (name + ".gltf"), root / (name + ".pcm")
        source.write_text(json.dumps(doc), encoding="utf-8")
        if not good:
            output.write_bytes(b"unchanged output")
        result = invoke(converter, "--emit-asset", "--section-directory",
                        *(["--pvr-emissive"] if profile else []),
                        *flags, source, output)
        assert "AddressSanitizer" not in result.stderr
        assert "runtime error:" not in result.stderr
        if not good:
            assert result.returncode != 0, name
            assert output.read_bytes() == b"unchanged output", name
            assert not list(root.glob(output.name + ".tmp.*"))
            return None
        assert result.returncode == 0, (name, result.stderr)
        assert "display-space additive approximation" in result.stderr
        return output.read_bytes()

    convert(document, "strict", good=False, profile=False)
    blob = convert(document, "joined", ("--join-strips",))
    assert struct.unpack_from("<I", blob, 12)[0] == 0  # Reserved, not features.
    parts = sections(blob)
    layers, uv, textures = parts[16][0], parts[17][0], parts[15][0]
    assert layers[:4] == b"PML1" and len(layers) == 96
    assert struct.unpack_from("<IIIHH", layers, 32) == (0, 0, 1, 2, 1)
    assert layers[48:53] == bytes([0, 0, 1, 2, 4])
    assert struct.unpack_from("<I6f", layers, 56) == (
        0xffffff, 1, 0, 0, 0, 1, 0)
    assert uv[:4] == b"PUV1"
    assert struct.unpack_from("<II", uv, 12) == (1, 1)
    assert struct.unpack_from("<4I", uv, 40) == (0, 0, 4, 0)
    assert struct.unpack_from("<2I", uv, 56) == (0, 0)
    expected_uv = (2.25, -1, -1.75, -1, 2.25, -0.5, -1.75, -0.5)
    assert struct.unpack_from("<8f", uv, 64) == expected_uv
    assert textures[:4] == b"PTX1"
    assert struct.unpack_from("<I", textures, 12)[0] == 2
    images = [struct.unpack_from("<HBBHHHHIII", textures, 64 + 32 * i)
              for i in range(2)]
    assert [x[0] for x in images] == [0, 1]
    assert images[0][1] == 0  # Base alpha retained, ARGB1555.
    assert images[1][1] == 1  # Emissive alpha ignored, RGB565.

    def encode(value, factor):
        v = value / 255
        linear = v / 12.92 if v <= 0.04045 else ((v + .055) / 1.055) ** 2.4
        linear *= factor
        srgb = linear * 12.92 if linear <= .0031308 else 1.055 * linear ** (1 / 2.4) - .055
        return math.floor(srgb * 255 + .5)

    red, green = encode(128, .25), encode(64, .5)
    texel = ((red >> 3) << 11) | ((green >> 2) << 5)
    assert textures[images[1][7]:images[1][7] + 128] == struct.pack("<H", texel) * 64
    assert textures[images[0][7]:images[0][7] + 128] == struct.pack("<H", 0x411f) * 64
    assert zlib.crc32(uv[40:]) == struct.unpack_from("<I", uv, 32)[0]
    assert zlib.crc32(layers[32:]) == struct.unpack_from("<I", layers, 20)[0]

    flipped = sections(convert(document, "flipped", ("--join-strips", "--flip-v")))
    assert struct.unpack_from("<8f", flipped[17][0], 64) == (
        2.25, 2, -1.75, 2, 2.25, 1.5, -1.75, 1.5)
    # No join and flipped winding retain authored corner occurrences.
    unjoined = sections(convert(document, "winding", ("--flip-winding",)))
    assert struct.unpack_from("<4I", unjoined[17][0], 40) == (0, 0, 6, 0)
    assert struct.unpack_from("<12f", unjoined[17][0], 64) == (
        2.25, -1, 2.25, -.5, -1.75, -1,
        2.25, -.5, -1.75, -.5, -1.75, -1)
    # Prepared/LZ4 paths use the same layer-aware prepublication loader.
    packed = sections(convert(document, "cooked", (
        "--join-strips", "--cooked-cache", "--lz4-vertices")))
    assert packed[16] == parts[16] and packed[17] == parts[17] and 10 in packed

    multiple = copy.deepcopy(document)
    multiple["materials"].append(copy.deepcopy(multiple["materials"][0]))
    multiple["materials"][1]["emissiveFactor"] = [1, 0, 0]
    multiple["meshes"].append(copy.deepcopy(multiple["meshes"][0]))
    multiple["meshes"][1]["primitives"][0]["material"] = 1
    multiple["nodes"].append({"mesh": 1})
    multiple["scenes"][0]["nodes"].append(1)
    multi = sections(convert(multiple, "multiple", ("--join-strips",)))
    assert struct.unpack_from("<II", multi[17][0], 12) == (2, 2)
    assert struct.unpack_from("<I", multi[15][0], 12)[0] == 3
    assert struct.unpack_from("<IIIHH", multi[16][0], 96) == (1, 0, 1, 2, 2)

    constant = copy.deepcopy(document)
    del constant["materials"][0]["emissiveTexture"]
    constant["materials"][0]["pbrMetallicRoughness"] = {}
    constant["textures"] = []
    const = sections(convert(constant, "constant", ("--join-strips",)))
    assert struct.unpack_from("<8f", const[17][0], 64) == (0,) * 8
    assert struct.unpack_from("<I", const[15][0], 12)[0] == 1
    constant_texel = ((encode(255, .25) >> 3) << 11) | ((encode(255, .5) >> 2) << 5)
    constant_offset = struct.unpack_from("<I", const[15][0], 76)[0]
    assert const[15][0][constant_offset:] == struct.pack("<H", constant_texel) * 64

    zero = copy.deepcopy(document)
    zero["materials"][0]["emissiveFactor"] = [0, 0, 0]
    zero_blob = convert(zero, "zero")
    assert struct.unpack_from("<I", zero_blob, 12)[0] == 0
    assert 16 not in sections(zero_blob) and 17 not in sections(zero_blob)
    # Fresh processes vary allocator placement. Earlier malloc-based blobs
    # intermittently violated the direct PTX1 loader's 32-byte alignment.
    for i in range(16):
        assert convert(zero, "zero-repeat-" + str(i)) == zero_blob
    for name, edit in (
        ("blend", lambda d: d["materials"][0].update(alphaMode="BLEND")),
        ("mask", lambda d: d["materials"][0].update(alphaMode="MASK")),
        ("occlusion", lambda d: d["materials"][0].update(occlusionTexture={"index": 0})),
        ("normal", lambda d: d["materials"][0].update(normalTexture={"index": 0})),
        ("uv-missing", lambda d: d["materials"][0]["emissiveTexture"].update(texCoord=2)),
        ("factor", lambda d: d["materials"][0].update(emissiveFactor=[2, 0, 0])),
        ("mipmap", lambda d: d["samplers"][0].update(minFilter=9987)),
        ("filters", lambda d: d["samplers"][0].update(minFilter=9729)),
        ("strength", lambda d: d["materials"][0].update(extensions={
            "KHR_materials_emissive_strength": {"emissiveStrength": 2}})),
    ):
        bad = copy.deepcopy(document)
        edit(bad)
        convert(bad, name, good=False)
    convert(document, "override", ("--texture-id", "1"), good=False)


if __name__ == "__main__":
    from test import invoke, png_rgba
    run(sys.argv[1], pathlib.Path(sys.argv[2]), invoke, png_rgba)
    print("emissive importer tests passed")
