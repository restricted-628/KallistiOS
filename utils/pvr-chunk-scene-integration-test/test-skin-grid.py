#!/usr/bin/env python3
"""Check authored grid generation and complete target serial reports.

Copyright (C) 2026 Joseph Black
"""

import base64
import json
import math
import pathlib
import struct
import sys
import unittest


MARKERS = (
    "KOSSCENE models=2 joints=2 morph_bindings=2 pose_goldens=6",
    "KOSSCENE grid_vertices=578 grid_triangles=1024 packet_guards=PASS uv_goldens=PASS",
    "KOSSCENE rotation_scale=PASS normal_goldens=PASS lighting_goldens=PASS",
    "KOSSCENE rendered=1 inspecting=1",
    "KOSSCENE grid_frames=240 triangles_per_frame=1024 packets_per_frame=1088 faults=0",
    "KOSSCENE result=PASS errno=0",
)
CLIP_MARKERS = MARKERS[:3] + (
    "KOSSCENE clip_checks=72 planes=6 pose_bounds=PASS area_uv_color=PASS guards=PASS",
    MARKERS[3],
    "KOSSCENE clip_frames=144 cases=6 faults=0",
    MARKERS[-1],
)


def check_log(content, markers=MARKERS):
    records = [line.strip() for line in content.splitlines()
               if line.startswith("KOSSCENE ")]
    assert records == list(markers), records


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
        normals = values(1, "f")
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
            self.assertEqual(positions[vertex * 3 + 2], positions[vertex * 3] / 2)
            self.assertAlmostEqual(normals[vertex * 3], -1 / math.sqrt(5), places=6)
            self.assertEqual(normals[vertex * 3 + 1], 0)
            self.assertAlmostEqual(normals[vertex * 3 + 2], 2 / math.sqrt(5), places=6)
        # All 512 triangles have distinct positions and winding matching
        # the fixture normals, independently of converter strip joining.
        for start in range(0, len(indices), 3):
            a, b, c = (positions[v * 3:v * 3 + 3] for v in indices[start:start + 3])
            cross_z = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            cross_x = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1])
            self.assertEqual(cross_z, 1 / 64)
            self.assertEqual(cross_x, -1 / 128)
        self.assertEqual(values(5, "f"), (0,) * (288 * 3) + (0.5, 0, 0))
        self.assertEqual(values(13, "f"), (1, 1, 1, 2, 1.5, 0.5, 1, 1, 1))
        rotations = values(12, "f")
        self.assertEqual(rotations[:4], (0, 0, 0, 1))
        self.assertEqual(rotations[8:], (0, 0, 0, 1))
        self.assertAlmostEqual(rotations[5], math.sqrt(0.5), places=6)
        self.assertAlmostEqual(rotations[7], math.sqrt(0.5), places=6)
        animation = scene["animations"][0]
        self.assertEqual([(c["target"]["node"], c["target"]["path"])
                          for c in animation["channels"]],
                         [(2, "translation"), (3, "weights"), (4, "weights"),
                          (2, "rotation"), (2, "scale")])

    def test_log(self):
        for markers in (MARKERS, CLIP_MARKERS):
            check_log("unrelated boot output\n" + "\n".join(markers), markers)
            for index in range(len(markers)):
                with self.assertRaises(AssertionError):
                    check_log("\n".join(markers[:index] + markers[index + 1:]), markers)
            for bad in (markers + (markers[-1],), tuple(reversed(markers)),
                        markers + ("KOSSCENE result=FAIL errno=5",),
                        tuple(line.replace("grid_frames=240", "grid_frames=239")
                                  .replace("clip_frames=144", "clip_frames=143") for line in markers)):
                with self.assertRaises(AssertionError):
                    check_log("\n".join(bad), markers)
        for key, value in (("clip_checks=72", "clip_checks=71"), ("planes=6", "planes=5"),
                           ("area_uv_color=PASS", "area_uv_color=FAIL"), ("cases=6", "cases=5"),
                           ("pose_bounds=PASS", "pose_bounds=FAIL"),
                           ("guards=PASS", "guards=FAIL"), ("faults=0", "faults=1")):
            with self.assertRaises(AssertionError):
                check_log("\n".join(CLIP_MARKERS).replace(key, value), CLIP_MARKERS)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("--log", "--clip-log"):
        if len(sys.argv) < 3:
            raise SystemExit("usage: test-skin-grid.py {--log|--clip-log} SERIAL_LOG [SERIAL_LOG ...]")
        for argument in sys.argv[2:]:
            path = pathlib.Path(argument)
            markers = CLIP_MARKERS if sys.argv[1] == "--clip-log" else MARKERS
            check_log(path.read_text(errors="replace"), markers)
            print(f"{path.name}: complete grid report and final cleanup PASS")
    else:
        unittest.main()
