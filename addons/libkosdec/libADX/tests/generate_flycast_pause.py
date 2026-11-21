from pathlib import Path
import struct

rate, frames = 44100, 196608
import sys

name = sys.argv[1]
assert name in ("proof", "fresh") and len(sys.argv) in (2, 3)
if len(sys.argv) == 3:
    frames = int(sys.argv[2])
    assert frames > 0 and frames % 32 == 0
state = 15106 if name == "proof" else 115901
def step():
    global state
    state = (state * 1664525 + 1013904223) & 0xffffffff
    return state >> 16

track = bytearray(32)
struct.pack_into('>HHBBBBIIHB', track, 0, 0x8000, 28, 3, 18, 4, 2, rate, frames, 500, 3)
track[-6:] = b'(c)CRI'
for _ in range(frames // 32):
    for _ in range(2):
        scale = 440 + step() % 224
        track.extend(struct.pack('>H', scale))
        for _ in range(8):
            amp = 1 + step() % 3
            if step() & 1:
                amp = -amp
            track.extend(((amp & 15) << 4 | ((-2*amp) & 15), (amp & 15) << 4))
track.extend(b'\x80\x01\x00\x0e' + bytes(14))
disc = Path("disc")
disc.mkdir(exist_ok=True)
(disc / (name + ".adx")).write_bytes(track)
