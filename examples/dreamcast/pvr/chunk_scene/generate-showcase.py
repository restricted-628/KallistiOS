#!/usr/bin/env python3
"""Author two flexible sleeves on the scene fixture's strip topology.

Copyright (C) 2026 Joseph Black
"""
import base64
import json
import math
from pathlib import Path
import struct
import sys


def generate(source, destination):
    scene = json.loads(source.read_text())
    blob = base64.b64decode(scene["buffers"][0]["uri"].split(",", 1)[1])
    payloads = [blob[v["byteOffset"]:v["byteOffset"] + v["byteLength"]]
                for v in scene["bufferViews"]]
    positions, normals, deltas, normal_deltas = [], [], [], []
    for row in range(17):
        t = row / 16
        radius = .32 + .13 * math.sin(math.pi * t)
        dr = .13 * math.pi * math.cos(math.pi * t) / 2.2
        bulge = .26 * math.sin(math.pi * t) ** 2
        db = .26 * math.pi * math.sin(2 * math.pi * t) / 2.2
        for column in range(17):
            theta = 2 * math.pi * column / 16
            c, s = math.cos(theta), math.sin(theta)
            positions.extend((radius * c, 2.2 * t - 1.1, radius * s))
            base_len = math.sqrt(1 + dr * dr)
            target_len = math.sqrt(1 + (dr + db) ** 2)
            n = (c / base_len, -dr / base_len, s / base_len)
            target = (c / target_len, -(dr + db) / target_len, s / target_len)
            normals.extend(n)
            deltas.extend((bulge * c, 0, bulge * s))
            normal_deltas.extend(b - a for a, b in zip(n, target))

    def replace(index, values):
        payloads[index] = struct.pack(f"<{len(values)}f", *values)

    replace(0, positions)
    replace(1, normals)
    replace(5, deltas)
    identity = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    replace(6, identity * 2)
    replace(8, (0, 0, 0, .35, 0, 0, 0, 0, 0))
    replace(9, (0, 0, 0))  # Left: skeletal animation only.
    replace(10, (0, 1, 0))  # Right: same skeleton plus a bulge morph.
    replace(12, (0, 0, 0, 1, 0, 0, math.sin(-.35), math.cos(-.35), 0, 0, 0, 1))
    replace(13, (1, 1, 1) * 3)
    normal_accessor = len(scene["accessors"])
    scene["accessors"].append({"bufferView": len(payloads), "componentType": 5126,
                               "count": 289, "type": "VEC3"})
    payloads.append(struct.pack(f"<{len(normal_deltas)}f", *normal_deltas))
    for mesh in scene["meshes"]:
        mesh["weights"] = [0]
        mesh["primitives"][0]["targets"][0]["NORMAL"] = normal_accessor
    scene["nodes"][0]["translation"] = [0, 0, 0]
    scene["nodes"][2]["translation"] = [0, 0, 0]
    scene["accessors"][0].update(min=[-.45, -1.1, -.45], max=[.45, 1.1, .45])
    # Remove stale authored bounds on changed animation/morph data.
    for index in (5, 8, 9, 10, 12, 13):
        scene["accessors"][index].pop("min", None)
        scene["accessors"][index].pop("max", None)
    for material, color in zip(scene["materials"],
                               ((.18, .82, 1, 1), (1, .48, .16, 1))):
        material["pbrMetallicRoughness"]["baseColorFactor"] = color
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
    scene["asset"]["generator"] = "KOS original flexible-sleeve showcase"
    destination.write_text(json.dumps(scene, indent=2) + "\n")


if __name__ == "__main__":
    generate(Path(sys.argv[1]), Path(sys.argv[2]))
