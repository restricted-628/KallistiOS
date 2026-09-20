#!/usr/bin/env python3
"""Expand the authored two-model scene into textured, blended-skin grids.

Copyright (C) 2026 Joseph Black
"""

import base64
import json
import pathlib
import struct
import sys


def generate(template, destination):
    scene = json.loads(template.read_text())
    original = base64.b64decode(scene["buffers"][0]["uri"].split(",", 1)[1])
    payloads = [original[v["byteOffset"]:v["byteOffset"] + v["byteLength"]]
                for v in scene["bufferViews"]]
    cells = 16
    side = cells + 1
    vertices = side * side
    positions, normals, joints, weights, morphs, uvs = [], [], [], [], [], []
    for row in range(side):
        for column in range(side):
            positions.extend((2 * column / cells - 1, 2 * row / cells - 1, 0))
            normals.extend((0, 0, 1))
            joints.extend((0, 1, 0, 0))
            weights.extend((1 - row / cells, row / cells, 0, 0))
            morphs.extend((0.5 if row == cells and column == cells else 0, 0, 0))
            uvs.extend((column / cells, row / cells))
    indices = []
    for row in range(cells):
        strip = [index for column in range(side)
                 for index in ((row + 1) * side + column, row * side + column)]
        for triangle in range(2 * cells):
            a, b, c = strip[triangle:triangle + 3]
            if triangle % 2:
                a, b = b, a
            indices.extend((a, b, c))
    for index, (kind, values) in enumerate(zip(
            ("f", "f", "H", "f", "H", "f"),
            (positions, normals, joints, weights, indices, morphs))):
        payloads[index] = struct.pack(f"<{len(values)}{kind}", *values)
        scene["accessors"][index]["count"] = len(indices) if index == 4 else vertices
    payloads.append(struct.pack(f"<{len(uvs)}f", *uvs))
    scene["accessors"].append({"bufferView": 11, "componentType": 5126,
                               "count": vertices, "type": "VEC2"})
    scene["accessors"][0].update(min=[-1, -1, 0], max=[1, 1, 0])
    for mesh in scene["meshes"]:
        mesh["primitives"][0]["attributes"]["TEXCOORD_0"] = 11
    packed = bytearray()
    scene["bufferViews"] = []
    for payload in payloads:
        packed.extend(b"\0" * (-len(packed) % 4))
        scene["bufferViews"].append({"buffer": 0, "byteOffset": len(packed),
                                      "byteLength": len(payload)})
        packed.extend(payload)
    scene["buffers"] = [{"byteLength": len(packed), "uri":
                         "data:application/octet-stream;base64," +
                         base64.b64encode(packed).decode("ascii")}]
    scene["asset"]["generator"] = "KOS authored animated textured grid fixture"
    destination.write_text(json.dumps(scene, indent=2) + "\n")


if __name__ == "__main__":
    generate(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
