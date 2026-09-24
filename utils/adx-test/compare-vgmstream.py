#!/usr/bin/env python3
"""Generate synthetic ADX, compare native PCM with an external vgmstream CLI.

No SDK inputs or external decoder source is copied into this repository.
Usage: python3 compare-vgmstream.py /path/to/vgmstream-cli
Build ./adx-decode first. All generated media lives in a temporary directory.
"""
import argparse
from pathlib import Path
import random
import struct
import subprocess
import tempfile
import wave


def make_input(version, channels, rate, cutoff, frames, offset, seed):
    header = bytearray(offset)
    struct.pack_into(">HHBBBBIIHBB", header, 0, 0x8000, offset - 4,
                     3, 18, 4, channels, rate, frames, cutoff, version, 0)
    header[-6:] = b"(c)CRI"
    rng = random.Random(seed)
    for block in range((frames + 31) // 32):
        for channel in range(channels):
            # Include multiplier-one, saturation, and ordinary amplitudes.
            scale = [0, 1, 31, 4095, 32767][(block + channel) % 5]
            header += struct.pack(">H", scale)
            header += bytes(rng.randrange(256) for _ in range(16))
    # Standard-shaped end record, outside the declared PCM count.
    header += b"\x80\x01\x00\x0e" + bytes(14)
    return header


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vgmstream", type=Path)
    args = parser.parse_args()
    native = Path(__file__).resolve().parent / "adx-decode"
    count = 0
    with tempfile.TemporaryDirectory(prefix="kos-adx-differential-") as directory:
        root = Path(directory)
        for version in (3, 5):
            for channels in (1, 2):
                for rate, cutoff in ((8000, 500), (22050, 500), (44100, 500),
                                     (48000, 500), (32000, 750), (44100, 1000)):
                    for frames in (1, 31, 32, 33, 193):
                        offset = (36, 288, 2048, 65539)[count % 4]
                        source, raw, reference = (root / name for name in
                                                 ("input.adx", "native.pcm", "reference.wav"))
                        source.write_bytes(make_input(version, channels, rate,
                                                      cutoff, frames, offset, count))
                        subprocess.run([str(native), str(source), str(raw)], check=True)
                        subprocess.run([str(args.vgmstream.resolve()), "-i", "-o",
                                        str(reference), str(source)], check=True,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                        with wave.open(str(reference), "rb") as wav:
                            assert wav.getnchannels() == channels
                            assert wav.getframerate() == rate
                            assert wav.getsampwidth() == 2
                            assert wav.getnframes() == frames
                            expected = wav.readframes(frames)
                        actual = raw.read_bytes()
                        if actual != expected:
                            samples = min(len(actual), len(expected)) // 2
                            a = struct.unpack("<" + "h" * samples, actual[:samples * 2])
                            b = struct.unpack("<" + "h" * samples, expected[:samples * 2])
                            first = next((i for i in range(samples) if a[i] != b[i]), None)
                            raise AssertionError((version, channels, rate, cutoff, frames,
                                                  first, a[first] if first is not None else None,
                                                  b[first] if first is not None else None))
                        count += 1
    print(f"PASS: {count} synthetic ADX files match vgmstream PCM byte-for-byte")


if __name__ == "__main__":
    main()
