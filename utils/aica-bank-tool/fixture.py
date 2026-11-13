#!/usr/bin/env python3
"""Create an original triangle-wave authoring fixture, not a game asset."""
import argparse
import json
from pathlib import Path
import struct
import wave


def create(directory):
    directory.mkdir(parents=True, exist_ok=False)
    pcm = [((i if i < 32 else 64 - i) * 512 - 8192) for i in range(64)]
    with wave.open(str(directory / 'triangle.wav'), 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(22050)
        wav.writeframes(struct.pack('<64h', *pcm))
    manifest = {
        'version': 1,
        'samples': [{'name': 'triangle', 'wav': 'triangle.wav',
                     'loop_start': 0, 'loop_end': 64}],
        'zones': [
            {'sample': 'triangle', 'root_key': 60, 'velocity_high': 63,
             'volume': 100, 'key_rate': 15},
            {'sample': 'triangle', 'root_key': 60, 'velocity_low': 64,
             'volume': 180, 'key_rate': 15},
            {'sample': 'triangle', 'drum': 1, 'key_low': 36, 'key_high': 36,
             'root_key': 36, 'volume': 128, 'decay1': 18, 'decay_level': 12,
             'decay2': 18, 'key_rate': 15},
        ],
    }
    (directory / 'bank.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('new_directory', type=Path)
    create(parser.parse_args().new_directory)
