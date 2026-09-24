#!/usr/bin/env python3
"""Indexed glTF normal preservation and stride-dependent batch regressions.

Copyright (C) 2026 Joseph Black
"""

import base64
import json
import math
import pathlib
import struct
import subprocess
import sys
import tempfile


def convert(converter, root, count, normals=True, colors=False, mixed=False):
    blob = bytearray()
    views, accessors = [], []

    def attribute(values, width, kind):
        index = len(views)
        payload = struct.pack(f"<{len(values)}f", *values)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(payload)})
        blob.extend(payload)
        accessors.append({"bufferView": index, "componentType": 5126,
                          "count": len(values) // width, "type": kind})
        return index

    points = ((0, 0, 0), (1, 0, 0.5), (0, 1, 0))
    attrs = {"POSITION": attribute([v for i in range(count) for v in points[i % 3]],
                                   3, "VEC3")}
    if normals:
        attrs["NORMAL"] = attribute([-0.5, 0, 1] * count, 3, "VEC3")
    if colors:
        attrs["COLOR_0"] = attribute([0.25, 0.5, 0.75, 1] * count, 4, "VEC4")
    index_view = len(views)
    views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": 6})
    blob.extend(struct.pack("<3H", 0, 1, 2))
    accessors.append({"bufferView": index_view, "componentType": 5123,
                      "count": 3, "type": "SCALAR"})
    primitive = {"attributes": attrs, "indices": index_view}
    primitives = [primitive]
    if mixed:
        primitives = [primitive, {"attributes": {"POSITION": attrs["POSITION"]},
                                  "indices": index_view}, primitive]
    doc = {"asset": {"version": "2.0"},
           "buffers": [{"byteLength": len(blob), "uri":
                        "data:application/octet-stream;base64," + base64.b64encode(blob).decode()}],
           "bufferViews": views, "accessors": accessors,
           "meshes": [{"primitives": primitives}], "nodes": [{"mesh": 0}],
           "scenes": [{"nodes": [0]}], "scene": 0}
    source, output = root / "normals.gltf", root / "normals.pcm"
    source.write_text(json.dumps(doc))
    result = subprocess.run([str(converter), "--emit-asset", "--section-directory",
                             str(source), str(output)], capture_output=True, text=True)
    assert result.returncode == 0 and not result.stderr, result
    data = output.read_bytes()
    kind, _, offset, size = struct.unpack_from("<4I", data, 64)
    assert kind == 1
    stream = data[offset:offset + size]
    cursor, expected_index, records = 0, 0, []
    while struct.unpack_from("<I", stream, cursor)[0] != 255:
        header, index_count = struct.unpack_from("<2I", stream, cursor)
        kind, words = header & 255, header >> 16
        first, vertices = index_count & 65535, index_count >> 16
        stride = {34: 3, 35: 4, 41: 6, 42: 7}[kind]
        assert first == expected_index and words == 1 + stride * vertices
        assert words <= 65535 and vertices > 0
        if kind in (41, 42):
            for vertex in (0, vertices - 1):
                normal = struct.unpack_from("<3f", stream, cursor + 20 + vertex * stride * 4)
                expected = (-1 / math.sqrt(5), 0, 2 / math.sqrt(5))
                assert all(abs(a - b) < 1e-6 for a, b in zip(normal, expected)), normal
        records.append((kind, vertices))
        expected_index += vertices
        cursor += 4 * (words + 1)
    assert cursor + 4 == len(stream)
    return records


def main():
    converter = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="kos-indexed-normals-") as temp:
        root = pathlib.Path(temp)
        assert convert(converter, root, 3) == [(41, 3)]
        assert convert(converter, root, 3, colors=True) == [(42, 3)]
        assert convert(converter, root, 3, mixed=True) == [(41, 3), (34, 3), (41, 3)]
        assert convert(converter, root, 3, colors=True, mixed=True) == [(42, 3), (35, 3), (42, 3)]
        assert convert(converter, root, 10923) == [(41, 10922), (41, 1)]
        assert convert(converter, root, 9363, colors=True) == [(42, 9362), (42, 1)]
        assert convert(converter, root, 16384, normals=False, colors=True) == [(35, 16383), (35, 1)]
    print("indexed glTF normals and 4/6/7-word batch boundaries passed")


if __name__ == "__main__":
    main()
