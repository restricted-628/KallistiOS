#!/usr/bin/env python3
"""Generate the independently authored, planar OBJ clipping fixture.

Copyright (C) 2026 Joseph Black
"""

import pathlib
import sys


def grid():
    lines = ["# KOS clipping workload: 32 x 16 cells, shared positions"]
    for y in range(17):
        for x in range(33):
            lines.append(f"v {x} {y} 0")
    lines.append("vn 0 0 1")
    for y in range(16):
        # Explicit strip-compatible face order; the converter joins adjacent
        # authored faces without searching/reordering the mesh topology.
        strip = [vertex for x in range(33)
                 for vertex in ((y + 1) * 33 + x + 1, y * 33 + x + 1)]
        for ordinal in range(64):
            a, b, c = strip[ordinal:ordinal + 3]
            if ordinal & 1:
                a, b = b, a
            lines.append(f"f {a}//1 {b}//1 {c}//1")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: generate-grid.py OUTPUT.obj")
    pathlib.Path(sys.argv[1]).write_text(grid(), encoding="ascii")
