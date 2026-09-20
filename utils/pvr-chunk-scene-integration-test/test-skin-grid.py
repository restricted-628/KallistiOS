#!/usr/bin/env python3
"""Check authored grid generation and complete target serial reports.

Copyright (C) 2026 Joseph Black
"""

import base64
import json
import pathlib
import struct
import sys
import unittest


MARKERS = (
    "KOSSCENE models=2 joints=2 morph_bindings=2 pose_goldens=6",
    "KOSSCENE grid_vertices=578 grid_triangles=1024 packet_guards=PASS uv_goldens=PASS",
    "KOSSCENE rendered=1 inspecting=1",
    "KOSSCENE grid_frames=240 triangles_per_frame=1024 packets_per_frame=1088 faults=0",
    "KOSSCENE result=PASS errno=0",
)


def check_log(content):
    records = [line.strip() for line in content.splitlines()
               if line.startswith("KOSSCENE ")]
    assert records == list(MARKERS), records


class GridTests(unittest.TestCase):
    def test_fixture(self):
        scene = json.loads(pathlib.Path("skin-grid.gltf").read_text())
        data = base64.b64decode(scene["buffers"][0]["uri"].split(",", 1)[1])

        def values(index, kind):
            view = scene["bufferViews"][index]
            width = struct.calcsize(kind)
            return struct.unpack_from(f'<{view["byteLength"] // width}{kind}',
                                      data, view["byteOffset"])

        positions = values(0, "f")
        weights = values(3, "f")
        indices = values(4, "H")
        uvs = values(11, "f")
        self.assertEqual(len(positions), 289 * 3)
        self.assertEqual(len(indices), 512 * 3)
        for vertex in range(289):
            row, column = divmod(vertex, 17)
            self.assertEqual(weights[vertex * 4:vertex * 4 + 4],
                             (1 - row / 16, row / 16, 0, 0))
            self.assertEqual(uvs[vertex * 2:vertex * 2 + 2],
                             (column / 16, row / 16))
        # All 512 triangles have distinct positions and +Z winding matching
        # the fixture normals, independently of converter strip joining.
        for start in range(0, len(indices), 3):
            a, b, c = (positions[v * 3:v * 3 + 3] for v in indices[start:start + 3])
            cross_z = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            self.assertEqual(cross_z, 1 / 64)
        self.assertEqual(values(5, "f"), (0,) * (288 * 3) + (0.5, 0, 0))

    def test_log(self):
        check_log("unrelated boot output\n" + "\n".join(MARKERS))
        for index in range(len(MARKERS)):
            with self.assertRaises(AssertionError):
                check_log("\n".join(MARKERS[:index] + MARKERS[index + 1:]))
        for bad in (MARKERS + (MARKERS[-1],), tuple(reversed(MARKERS)),
                    MARKERS + ("KOSSCENE result=FAIL errno=5",),
                    tuple(line.replace("grid_frames=240", "grid_frames=239") for line in MARKERS)):
            with self.assertRaises(AssertionError):
                check_log("\n".join(bad))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--log":
        if len(sys.argv) < 3:
            raise SystemExit("usage: test-skin-grid.py --log SERIAL_LOG [SERIAL_LOG ...]")
        for argument in sys.argv[2:]:
            path = pathlib.Path(argument)
            check_log(path.read_text(errors="replace"))
            print(f"{path.name}: grid goldens, 240 frames, and final cleanup PASS")
    else:
        unittest.main()
