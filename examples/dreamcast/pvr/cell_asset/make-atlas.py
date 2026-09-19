#!/usr/bin/env python3
"""Generate the deterministic 64x32 RGBA atlas used by the example.

Copyright (C) 2026 Joseph Black
"""

import pathlib
import struct
import sys
import zlib


WIDTH = 64
HEIGHT = 32
COLORS = (
    (255, 48, 32, 255),
    (255, 192, 32, 255),
    (48, 255, 48, 255),
    (48, 255, 255, 255),
    (48, 80, 255, 128),
    (255, 48, 255, 128),
    (0, 0, 0, 0),
    (0, 0, 0, 0),
)


def chunk(kind, payload):
    body = kind + payload
    checksum = zlib.crc32(body) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", checksum)


def pixel(x, y):
    region = (y // 16) * 4 + x // 16
    red, green, blue, alpha = COLORS[region]
    if region in (2, 3) and ((x // 4) ^ (y // 4)) & 1:
        alpha = 0
    return bytes((red, green, blue, alpha))


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: make-atlas.py OUTPUT.png")
    rows = b"".join(
        b"\0" + b"".join(pixel(x, y) for x in range(WIDTH))
        for y in range(HEIGHT)
    )
    image = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT,
                                      8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, 9))
        + chunk(b"IEND", b"")
    )
    pathlib.Path(sys.argv[1]).write_bytes(image)


if __name__ == "__main__":
    main()
